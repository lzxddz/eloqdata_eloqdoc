// @tags: [requires_non_retryable_writes, requires_fastcount]

t = db.remove6;
t.drop();

N = 1000;

function pop() {
    t.drop();
    var arr = [];
    for (var i = 0; i < N; i++) {
        arr.push({x: 1, tags: ["a", "b", "c"]});
    }
    t.insert(arr);
    assert(t.count() / N > 0.8 && t.count() / N < 1.2);
    // assert.eq(t.count(), N);
}

function del() {
    return t.remove({tags: {$in: ["a", "c"]}});
}

function test(n, idx) {
    pop();
    // assert.eq(N, t.count(), n + " A " + idx);
    assert(N / t.count() > 0.8 && N / t.count() < 1.2);
    if (idx)
        t.ensureIndex(idx);
    var res = del();
    assert(!res.hasWriteError(), "error deleting: " + res.toString());
    assert.eq(0, t.count(), n + " B " + idx);
}

test("a");
test("b", {x: 1});
test("c", {tags: 1});

N = 5000;

test("a2");
test("b2", {x: 1});
test("c2", {tags: 1});
