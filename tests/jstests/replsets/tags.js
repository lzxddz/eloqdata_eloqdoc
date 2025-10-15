(function() {
    'use strict';

    load("tests/jstests/replsets/libs/tags.js");

    let nodes = [{}, {}, {}, {}, {}];
    new TagsTest({nodes: nodes}).run();
}());
