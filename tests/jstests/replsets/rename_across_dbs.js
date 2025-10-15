(function() {
    'use strict';

    load("tests/jstests/replsets/libs/rename_across_dbs.js");

    new RenameAcrossDatabasesTest().run();
}());
