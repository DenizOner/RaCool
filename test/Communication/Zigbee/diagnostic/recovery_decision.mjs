const valid=value=>Number.isInteger(value) && value>=0 && value<=600;

export function decideRecovery(journal,actual) {
    if(!journal || !valid(journal.baseline) || !valid(journal.confirmed) ||
        typeof journal.uncertain!=='boolean' || !valid(actual)) throw Error('Invalid recovery evidence');
    if(journal.uncertain) return 'delivery_unknown';
    if(actual===journal.baseline) return 'already_restored';
    if(actual===journal.confirmed) return 'restore';
    return 'foreign_change';
}

export async function recoverOwnedValue(journal,io) {
    const before=await io.read();
    const decision=decideRecovery(journal,before);
    if(decision!=='restore') return {status:decision,before};
    await io.write(journal.baseline);
    const after=await io.read();
    if(after!==journal.baseline) throw Error('Restored value did not match correlated readback');
    return {status:'restored',before,after};
}
