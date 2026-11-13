import test from 'node:test';
import assert from 'node:assert/strict';
import {decideRecovery,recoverOwnedValue} from './diagnostic/recovery_decision.mjs';

test('only a confirmed owned value can be restored',()=>{
    const journal={baseline:3,confirmed:0,uncertain:false};
    assert.equal(decideRecovery(journal,0),'restore');
    assert.equal(decideRecovery(journal,3),'already_restored');
    assert.equal(decideRecovery(journal,9),'foreign_change');
});

test('an uncertain delivery is never replayed, even if baseline is observed',()=>{
    for(const actual of [0,3,9])
        assert.equal(decideRecovery({baseline:3,confirmed:0,uncertain:true},actual),'delivery_unknown');
});

test('missing and non-finite values are rejected',()=>{
    for(const journal of [{baseline:3,uncertain:false},{baseline:NaN,confirmed:1,uncertain:false},null])
        assert.throws(()=>decideRecovery(journal,1));
    assert.throws(()=>decideRecovery({baseline:3,confirmed:1,uncertain:false},Infinity));
});

test('confirmed owned value is restored once and checked by a fresh read',async()=>{
    let value=0, writes=0, reads=0;
    const result=await recoverOwnedValue({baseline:3,confirmed:0,uncertain:false},{
        read:async()=>{reads++;return value;},
        write:async next=>{writes++;value=next;}
    });
    assert.deepEqual(result,{status:'restored',before:0,after:3});
    assert.equal(reads,2);assert.equal(writes,1);
});

test('foreign value and uncertain delivery cause no write',async()=>{
    for(const [journal,value,status] of [
        [{baseline:3,confirmed:0,uncertain:false},9,'foreign_change'],
        [{baseline:3,confirmed:0,uncertain:true},0,'delivery_unknown'],
        [{baseline:3,confirmed:0,uncertain:false},3,'already_restored']]) {
        let writes=0;
        const result=await recoverOwnedValue(journal,{read:async()=>value,write:async()=>{writes++;}});
        assert.equal(result.status,status);assert.equal(writes,0);
    }
});

test('restore failure and mismatched readback never cause a second write',async()=>{
    for(const fail of [true,false]) {
        let writes=0;
        await assert.rejects(recoverOwnedValue({baseline:3,confirmed:0,uncertain:false},{
            read:async()=>0,
            write:async()=>{writes++;if(fail)throw Error('delivery unknown');}
        }));
        assert.equal(writes,1);
    }
});
