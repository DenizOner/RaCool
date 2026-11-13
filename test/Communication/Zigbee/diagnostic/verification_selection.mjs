// Resolve the entire requested scope before any device setting is changed.
export function selectVerificationExposes(exposes, requested = []) {
    const eligible = exposes.filter(e => (e.access & 2) && (e.access & 4) &&
        e.name !== "system_command" && !/^external_temperature_[123]$/.test(e.name) && ["enum", "numeric"].includes(e.type));
    if (!requested.length) return eligible;
    const selected = new Set();
    for (const name of requested) {
        const matches = eligible.filter(e => e.name === name || e.property === name);
        if (matches.length !== 1) throw Error("Verification control must match exactly one readable/writable control: " + name);
        selected.add(matches[0]);
    }
    return [...selected];
}


// Explicit read scope may include telemetry; it must never admit a power command.
export function selectReadExposes(exposes, requested) {
    if (!Array.isArray(requested) || !requested.length) throw Error('Read scope must be explicit');
    const eligible=exposes.filter(expose=>(expose.access & 4) && expose.name !== 'system_command' &&
        ['enum','numeric','binary'].includes(expose.type));
    const selected=new Set();
    for (const name of requested) {
        const matches=eligible.filter(expose=>expose.name === name || expose.property === name);
        if (matches.length !== 1) throw Error('Read control must match exactly one readable expose: '+name);
        selected.add(matches[0]);
    }
    return [...selected];
}
