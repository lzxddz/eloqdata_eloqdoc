// Cannot implicitly shard accessed collections because of extra shard key index in sharded
// collection.
// @tags: [assumes_no_implicit_index_creation]

t = db.bench_test1;
t.drop();

t.insert({_id: 1, x: 1});
t.insert({_id: 2, x: 1});

ops = [
    {op: "findOne", ns: t.getFullName(), query: {_id: 1}},
    {op: "update", ns: t.getFullName(), query: {_id: 1}, update: {$inc: {x: 1}}}
];

seconds = 10;

benchArgs = {
    ops: ops,
    parallel: 2,
    seconds: seconds,
    host: db.getMongo().host
};

if (jsTest.options().auth) {
    benchArgs['db'] = 'admin';
    benchArgs['username'] = jsTest.options().authUser;
    benchArgs['password'] = jsTest.options().authPassword;
}
res = benchRun(benchArgs);

// assert.lte(seconds * res.update, t.findOne({_id: 1}).x * 2, "A1");
assert.lte(seconds * res.update, t.findOne({_id: 1}).x * 3, "A1");

assert.eq(1, t.getIndexes().length, "B1");
benchArgs['ops'] = [{op: "createIndex", ns: t.getFullName(), key: {x: 1}}];
benchArgs['parallel'] = 1;
benchRun(benchArgs);
assert.eq(2, t.getIndexes().length, "B2");
benchArgs['ops'] = [{op: "dropIndex", ns: t.getFullName(), key: {x: 1}}];
benchRun(benchArgs);
assert.soon(function() {
    return t.getIndexes().length == 1;
});
