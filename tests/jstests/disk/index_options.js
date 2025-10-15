var baseName = "jstests_disk_index_options";

load('tests/jstests/libs/command_line/test_parsed_options.js');

jsTest.log("Testing \"noIndexBuildRetry\" command line option");
var expectedResult = {"parsed": {"storage": {"indexBuildRetry": false}}};
testGetCmdLineOptsMongod({noIndexBuildRetry: ""}, expectedResult);

jsTest.log("Testing \"storage.indexBuildRetry\" config file option");
expectedResult = {
    "parsed": {
        "config": "tests/jstests/libs/config_files/enable_indexbuildretry.json",
        "storage": {"indexBuildRetry": true}
    }
};
testGetCmdLineOptsMongod({config: "tests/jstests/libs/config_files/enable_indexbuildretry.json"},
                         expectedResult);

jsTest.log("Testing with no explicit index option setting");
expectedResult = {
    "parsed": {"storage": {}}
};
testGetCmdLineOptsMongod({}, expectedResult);

jsTest.log("Testing explicitly disabled \"noIndexBuildRetry\" config file option");
expectedResult = {
    "parsed": {
        "config": "tests/jstests/libs/config_files/disable_noindexbuildretry.ini",
        "storage": {"indexBuildRetry": true}
    }
};
testGetCmdLineOptsMongod({config: "tests/jstests/libs/config_files/disable_noindexbuildretry.ini"},
                         expectedResult);

print(baseName + " succeeded.");
