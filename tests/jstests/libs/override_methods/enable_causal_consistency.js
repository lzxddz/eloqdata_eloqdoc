/**
 * Enables causal consistency on the connections.
 */
(function() {
    "use strict";

    load("tests/jstests/libs/override_methods/override_helpers.js");
    load('tests/jstests/libs/override_methods/set_read_preference_secondary.js');

    db.getMongo().setCausalConsistency();

    OverrideHelpers.prependOverrideInParallelShell(
        "tests/jstests/libs/override_methods/enable_causal_consistency.js");
})();
