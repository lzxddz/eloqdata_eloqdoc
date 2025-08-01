/**
 *    Copyright (C) 2017 MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kWrite

#include "mongo/platform/basic.h"

#include "mongo/db/session_catalog.h"

#include <boost/optional.hpp>

#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/kill_sessions_common.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/rpc/get_status_from_command_result.h"
#include "mongo/stdx/memory.h"
#include "mongo/transport/service_state_machine.h"
#include "mongo/util/log.h"

namespace mongo {
namespace {

const auto sessionTransactionTableDecoration = ServiceContext::declareDecoration<SessionCatalog>();

const auto operationSessionDecoration =
    OperationContext::declareDecoration<boost::optional<ScopedCheckedOutSession>>();

}  // namespace

extern thread_local int16_t localThreadId;

void SessionCatalog::reset() {
    _txnTable.clear();
}

SessionCatalog::~SessionCatalog() {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    for (const auto& entry : _txnTable) {
        auto& sri = entry.second;
        invariant(!sri->checkedOut);
    }
}

void SessionCatalog::reset_forTest() {
    stdx::lock_guard<stdx::mutex> lg(_mutex);
    _txnTable.clear();
}

SessionCatalog* SessionCatalog::get(OperationContext* opCtx) {
    return get(opCtx->getServiceContext());
}

SessionCatalog* SessionCatalog::get(ServiceContext* service) {
    auto& sessionTransactionTable = sessionTransactionTableDecoration(service);
    return &sessionTransactionTable;
}

boost::optional<UUID> SessionCatalog::getTransactionTableUUID(OperationContext* opCtx) {
    AutoGetCollection autoColl(opCtx, NamespaceString::kSessionTransactionsTableNamespace, MODE_IS);

    const auto coll = autoColl.getCollection();
    if (coll == nullptr) {
        return boost::none;
    }

    return coll->uuid();
}

void SessionCatalog::onStepUp(OperationContext* opCtx) {
    invalidateSessions(opCtx, boost::none);

    DBDirectClient client(opCtx);

    const size_t initialExtentSize = 0;
    const bool capped = false;
    const bool maxSize = 0;

    BSONObj result;

    if (client.createCollection(NamespaceString::kSessionTransactionsTableNamespace.ns(),
                                initialExtentSize,
                                capped,
                                maxSize,
                                &result)) {
        return;
    }

    const auto status = getStatusFromCommandResult(result);

    if (status == ErrorCodes::NamespaceExists) {
        return;
    }

    uassertStatusOKWithContext(
        status,
        str::stream() << "Failed to create the "
                      << NamespaceString::kSessionTransactionsTableNamespace.ns() << " collection");
}

ScopedCheckedOutSession SessionCatalog::checkOutSession(OperationContext* opCtx) {
    invariant(!opCtx->lockState()->isLocked());
    invariant(opCtx->getLogicalSessionId());

    const auto lsid = *opCtx->getLogicalSessionId();

#ifndef D_USE_CORO_SYNC
    stdx::unique_lock<stdx::mutex> ul(_mutex);
#else
    const CoroutineFunctors& coro = opCtx->getCoroutineFunctors();
    stdx::unique_lock<stdx::mutex> ul(_mutex, std::defer_lock);
    while (!ul.try_lock()) {
        (*coro.longResumeFuncPtr)();
        (*coro.yieldFuncPtr)();
    }
#endif

    invariant(localThreadId >= 0);
    uint16_t threadGroupId = localThreadId;
    auto sri = _getOrCreateSessionRuntimeInfo(ul, opCtx, lsid, threadGroupId);

    // Wait until the session is no longer checked out
    opCtx->waitForConditionOrInterrupt(
        sri->availableCondVar, ul, [&sri]() { return !sri->checkedOut; });

    invariant(!sri->checkedOut);
    sri->checkedOut = true;

    invariant(ul.owns_lock());

    const Session& session = sri->txnState;
    invariant(session.getSessionId() == lsid);
    if (uint16_t orig = session.ThreadGroupId();
        orig != threadGroupId && session.inMultiDocumentTransaction()) {
        log() << "Migrate session " << session.getSessionId().getId() << ". Current ThreadGroup "
              << threadGroupId << ". Original ThreadGroup " << orig;
        Client* client = Client::getCurrent();
        ServiceStateMachine* ssm = client->getServiceStateMachine();
        ssm->migrateThreadGroup(orig);
        log() << "Migrate session " << session.getSessionId().getId() << " done.";
        invariant(Client::getCurrent() == client);
    }

    return ScopedCheckedOutSession(opCtx, ScopedSession(std::move(sri)));
}

ScopedSession SessionCatalog::getOrCreateSession(OperationContext* opCtx,
                                                 const LogicalSessionId& lsid) {
    invariant(!opCtx->lockState()->isLocked());
    invariant(!opCtx->getLogicalSessionId());
    invariant(!opCtx->getTxnNumber());

    auto ss = [&] {
#ifndef D_USE_CORO_SYNC
        stdx::unique_lock<stdx::mutex> ul(_mutex);
#else
        const CoroutineFunctors& coro = opCtx->getCoroutineFunctors();
        stdx::unique_lock<stdx::mutex> ul(_mutex, std::defer_lock);
        while (!ul.try_lock()) {
            (*coro.longResumeFuncPtr)();
            (*coro.yieldFuncPtr)();
        }
#endif
        invariant(localThreadId >= 0);
        uint16_t threadGroupId = localThreadId;
        return ScopedSession(_getOrCreateSessionRuntimeInfo(ul, opCtx, lsid, threadGroupId));
    }();

    // Perform the refresh outside of the mutex
    ss->refreshFromStorageIfNeeded(opCtx);

    return ss;
}

void SessionCatalog::invalidateSessions(OperationContext* opCtx,
                                        boost::optional<BSONObj> singleSessionDoc) {
    auto replCoord = repl::ReplicationCoordinator::get(opCtx);
    bool isReplSet = replCoord->getReplicationMode() == repl::ReplicationCoordinator::modeReplSet;
    if (isReplSet) {
        uassert(40528,
                str::stream() << "Direct writes against "
                              << NamespaceString::kSessionTransactionsTableNamespace.ns()
                              << " cannot be performed using a transaction or on a session.",
                !opCtx->getLogicalSessionId());
    }

    const auto invalidateSessionFn = [&](WithLock, SessionRuntimeInfoMap::iterator it) {
        auto& sri = it->second;
        sri->txnState.invalidate();

        // We cannot remove checked-out sessions from the cache, because operations expect to find
        // them there to check back in
        if (!sri->checkedOut) {
            _txnTable.erase(it);
        }
    };

#ifndef D_USE_CORO_SYNC
    stdx::lock_guard<stdx::mutex> lg(_mutex);
#else
    const CoroutineFunctors& coro = opCtx->getCoroutineFunctors();
    stdx::unique_lock<stdx::mutex> lg(_mutex, std::defer_lock);
    while (!lg.try_lock()) {
        (*coro.longResumeFuncPtr)();
        (*coro.yieldFuncPtr)();
    }
#endif

    if (singleSessionDoc) {
        const auto lsid = LogicalSessionId::parse(IDLParserErrorContext("lsid"),
                                                  singleSessionDoc->getField("_id").Obj());

        auto it = _txnTable.find(lsid);
        if (it != _txnTable.end()) {
            invalidateSessionFn(lg, it);
        }
    } else {
        auto it = _txnTable.begin();
        while (it != _txnTable.end()) {
            invalidateSessionFn(lg, it++);
        }
    }
}

void SessionCatalog::scanSessions(OperationContext* opCtx,
                                  const SessionKiller::Matcher& matcher,
                                  stdx::function<void(OperationContext*, Session*)> workerFn) {
    stdx::lock_guard<stdx::mutex> lg(_mutex);

    LOG(2) << "Beginning scanSessions. Scanning " << _txnTable.size() << " sessions.";

    for (auto it = _txnTable.begin(); it != _txnTable.end(); ++it) {
        // TODO SERVER-33850: Rename KillAllSessionsByPattern and
        // ScopedKillAllSessionsByPatternImpersonator to not refer to session kill.
        if (const KillAllSessionsByPattern* pattern = matcher.match(it->first)) {
            ScopedKillAllSessionsByPatternImpersonator impersonator(opCtx, *pattern);
            workerFn(opCtx, &(it->second->txnState));
        }
    }
}

std::shared_ptr<SessionCatalog::SessionRuntimeInfo> SessionCatalog::_getOrCreateSessionRuntimeInfo(
    WithLock, OperationContext* opCtx, const LogicalSessionId& lsid, uint16_t threadGroupId) {
    invariant(!opCtx->lockState()->inAWriteUnitOfWork());

    auto it = _txnTable.find(lsid);
    if (it == _txnTable.end()) {
        it = _txnTable.emplace(lsid, std::make_shared<SessionRuntimeInfo>(lsid, threadGroupId))
                 .first;
    }

    return it->second;
}

void SessionCatalog::_releaseSession(OperationContext* opCtx, const LogicalSessionId& lsid) {
#ifndef D_USE_CORO_SYNC
    stdx::lock_guard<stdx::mutex> lg(_mutex);
#else
    const CoroutineFunctors& coro = opCtx->getCoroutineFunctors();
    stdx::unique_lock<stdx::mutex> lg(_mutex, std::defer_lock);
    while (!lg.try_lock()) {
        (*coro.longResumeFuncPtr)();
        (*coro.yieldFuncPtr)();
    }
#endif

    auto it = _txnTable.find(lsid);
    invariant(it != _txnTable.end());

    auto& sri = it->second;
    invariant(sri->checkedOut);

    sri->checkedOut = false;
    sri->availableCondVar.notify_one();
}

OperationContextSession::OperationContextSession(OperationContext* opCtx,
                                                 bool checkOutSession,
                                                 boost::optional<bool> autocommit,
                                                 boost::optional<bool> startTransaction,
                                                 StringData dbName,
                                                 StringData cmdName)
    : _opCtx(opCtx) {

    if (!opCtx->getLogicalSessionId()) {
        return;
    }

    if (!checkOutSession) {
        return;
    }

    auto& checkedOutSession = operationSessionDecoration(opCtx);
    if (!checkedOutSession) {
        auto sessionTransactionTable = SessionCatalog::get(opCtx);
        auto scopedCheckedOutSession = sessionTransactionTable->checkOutSession(opCtx);
        // We acquire a Client lock here to guard the construction of this session so that
        // references to this session are safe to use while the lock is held.
        stdx::lock_guard<Client> lk(*opCtx->getClient());
        checkedOutSession.emplace(std::move(scopedCheckedOutSession));
    } else {
        // The only reason to be trying to check out a session when you already have a session
        // checked out is if you're in DBDirectClient.
        invariant(opCtx->getClient()->isInDirectClient());
        return;
    }

    const auto session = checkedOutSession->get();
    invariant(opCtx->getLogicalSessionId() == session->getSessionId());

    checkedOutSession->get()->refreshFromStorageIfNeeded(opCtx);

    if (opCtx->getTxnNumber()) {
        checkedOutSession->get()->beginOrContinueTxn(
            opCtx, *opCtx->getTxnNumber(), autocommit, startTransaction, dbName, cmdName);
    }
    session->setCurrentOperation(opCtx);
}

OperationContextSession::~OperationContextSession() {
    // Only release the checked out session at the end of the top-level request from the client,
    // not at the end of a nested DBDirectClient call.
    if (_opCtx->getClient()->isInDirectClient()) {
        return;
    }

    auto& checkedOutSession = operationSessionDecoration(_opCtx);
    if (checkedOutSession) {
        checkedOutSession->get()->clearCurrentOperation();
    }
    // We acquire a Client lock here to guard the destruction of this session so that references to
    // this session are safe to use while the lock is held.
    stdx::lock_guard<Client> lk(*_opCtx->getClient());
    checkedOutSession.reset();
}

Session* OperationContextSession::get(OperationContext* opCtx) {
    auto& checkedOutSession = operationSessionDecoration(opCtx);
    if (checkedOutSession) {
        return checkedOutSession->get();
    }

    return nullptr;
}

}  // namespace mongo
