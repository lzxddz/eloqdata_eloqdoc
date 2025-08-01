/**
 *    Copyright (C) 2016 MongoDB Inc.
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

#pragma once

#include "mongo/base/disallow_copying.h"
#include "mongo/db/dbmessage.h"
#include "mongo/transport/service_executor.h"
#include "mongo/transport/session.h"

namespace mongo {

/**
 * This is the entrypoint from the transport layer into mongod or mongos.
 *
 * The ServiceEntryPoint accepts new Sessions from the TransportLayer, and is
 * responsible for running these Sessions in a get-Message, run-Message,
 * reply-with-Message loop.  It may not do this on the TransportLayer’s thread.
 */
class ServiceEntryPoint {
    MONGO_DISALLOW_COPYING(ServiceEntryPoint);

public:
    /**
     * Stats for sessions open.
     */
    struct Stats {
        /**
         * Returns the number of sessions currently open.
         */
        size_t numOpenSessions = 0;

        /**
         * Returns the total number of sessions that have ever been created.
         */
        size_t numCreatedSessions = 0;

        /**
         * Returns the number of available sessions we could still open. Only relevant
         * when we are operating under a transport::Session limit (for example, in the
         * legacy implementation, we respect a maximum number of connections). If there
         * is no session limit, returns std::numeric_limits<int>::max().
         */
        size_t numAvailableSessions = 0;
    };

    virtual ~ServiceEntryPoint() = default;

    virtual Status start() = 0;

    /**
     * Begin running a new Session. This method returns immediately.
     */
    virtual void startSession(transport::SessionHandle session) = 0;

    /**
     * End all sessions that do not match the mask in tags.
     */
    virtual void endAllSessions(transport::Session::TagMask tags) = 0;

    /**
     * Shuts down the service entry point.
     */
    virtual bool shutdown(Milliseconds timeout) = 0;

    /**
     * Returns high-level stats about current sessions.
     */
    virtual Stats sessionStats() const = 0;

    /**
     * Returns the number of sessions currently open.
     */
    virtual size_t numOpenSessions() const = 0;

    /**
     * Processes a request and fills out a DbResponse.
     */
    virtual DbResponse handleRequest(OperationContext* opCtx, const Message& request) = 0;

    virtual transport::ServiceExecutor* getServiceExecutor() = 0;

protected:
    ServiceEntryPoint() = default;
};

}  // namespace mongo
