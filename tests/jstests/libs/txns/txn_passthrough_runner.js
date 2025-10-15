(function() {
    'use strict';

    load('tests/jstests/libs/override_methods/enable_sessions.js');
    load('tests/jstests/libs/txns/txn_override.js');

    const testFile = TestData.multiStmtTxnTestFile;

    try {
        load(testFile);
    } finally {
        // Run a lightweight command to allow the override file to commit the last command.
        // Ensure this command runs even if the test errors.
        assert.commandWorked(db.runCommand({ping: 1}));
    }
})();
