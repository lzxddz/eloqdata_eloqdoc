// Cannot implicitly shard accessed collections because of following errmsg: A single
// update/delete on a sharded collection must contain an exact match on _id or contain the shard
// key.
// @tags: [assumes_unsharded_collection]

// Test that we can update DBRefs, but not dbref fields outside a DBRef

var res;
t = db.jstests_update_dbref;
t.drop();

res = t.save({_id: 1, a: new DBRef("a", "b")});
assert(!res.hasWriteError(), "failed to save dbref");
assert.docEq({_id: 1, a: new DBRef("a", "b")}, t.findOne());

res = t.update({}, {$set: {"a.$id": 2}});
assert(!res.hasWriteError(), "a.$id update");
assert.docEq({_id: 1, a: new DBRef("a", 2)}, t.findOne());

res = t.update({}, {$set: {"a.$ref": "b"}});
assert(!res.hasWriteError(), "a.$ref update");

assert.docEq({_id: 1, a: new DBRef("b", 2)}, t.findOne());

// Bad updates
res = t.update({}, {$set: {"$id": 3}});
assert.writeError(res);
// assert(/\$id/.test(res.getWriteError()), "expected bad update because of $id");
assert(/\$id/.test(res.errmsg), "expected bad update because of $id");
assert.docEq({_id: 1, a: new DBRef("b", 2)}, t.findOne());

res = t.update({}, {$set: {"$ref": "foo"}});
assert.writeError(res);
// assert(/\$ref/.test(res.getWriteError()), "expected bad update because of $ref");
assert(/\$ref/.test(res.errmsg), "expected bad update because of $ref");
assert.docEq({_id: 1, a: new DBRef("b", 2)}, t.findOne());

res = t.update({}, {$set: {"$db": "aDB"}});
assert.writeError(res);
// assert(/\$db/.test(res.getWriteError()), "expected bad update because of $db");
assert(/\$db/.test(res.errmsg), "expected bad update because of $db");
assert.docEq({_id: 1, a: new DBRef("b", 2)}, t.findOne());

res = t.update({}, {$set: {"b.$id": 2}});
// assert(res.hasWriteError(),
//        "b.$id update should fail -- doc:" + tojson(t.findOne()) + " result:" + res.toString());
assert.commandFailedWithCode(res, ErrorCodes.InvalidDBRef,
       "b.$id update should fail -- doc:" + tojson(t.findOne()) + " result:" + res.toString());

res = t.update({}, {$set: {"b.$ref": 2}});
// assert(res.hasWriteError(),
//        "b.$ref update should fail -- doc:" + tojson(t.findOne()) + " result:" + res.toString());
assert.commandFailedWithCode(res, ErrorCodes.InvalidDBRef,
       "b.$ref update should fail -- doc:" + tojson(t.findOne()) + " result:" + res.toString());
