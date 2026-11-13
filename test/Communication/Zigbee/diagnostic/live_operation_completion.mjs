// Persist only after the operation's finally blocks have settled.
export async function settleBeforeSnapshot(operation, result, save) {
    try {
        if (operation) await operation;
    } catch (error) {
        result.completed = false;
        result.operationError = error instanceof Error ? error.message : String(error);
    }
    await save(result);
}
