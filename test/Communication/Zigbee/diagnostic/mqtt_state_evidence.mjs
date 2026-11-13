// MQTT observation is not proof of a correlated ZCL read or physical actuation.
export class MqttStateEvidence {
    #waiters = new Set();
    #closed = false;
    sequence = 0;
    latest;
    observe(state, retained, timestamp = Date.now()) {
        if (this.#closed || retained || !state || typeof state !== 'object' || Array.isArray(state)) return;
        this.latest = {state, sequence: ++this.sequence, timestamp};
        for (const waiter of this.#waiters) waiter();
    }
    close() {
        this.#closed = true;
        for (const waiter of this.#waiters) waiter();
    }
    matchAfter(sequence, property, expected) {
        if (!Number.isInteger(sequence) || sequence < 0) return undefined;
        const sample = this.latest;
        if (!sample || sample.sequence <= sequence ||
            !Object.hasOwn(sample.state, property) ||
            JSON.stringify(sample.state[property]) !== JSON.stringify(expected)) return undefined;
        return {kind: 'mqtt_state_after_get', sequence: sample.sequence,
            timestamp: sample.timestamp, actual: sample.state[property]};
    }
    async waitAfter(sequence, property, expected, timeoutMs = 10000) {
        if (!Number.isInteger(sequence) || sequence < 0 || typeof property !== 'string' || !property ||
            !Number.isFinite(timeoutMs) || timeoutMs <= 0 || timeoutMs > 60000)
            throw new Error('Invalid bounded MQTT observation request');
        if (this.#closed) return undefined;
        const match = () => {
            const sample = this.latest;
            if (expected !== undefined) return this.matchAfter(sequence, property, expected);
            if (!sample || sample.sequence <= sequence || !Object.hasOwn(sample.state, property)) return undefined;
            return {kind: 'mqtt_state_after_get', sequence: sample.sequence,
                timestamp: sample.timestamp, actual: sample.state[property]};
        };
        const existing = match();
        if (existing) return existing;
        return new Promise(resolve => {
            const finish = value => { clearTimeout(timer); this.#waiters.delete(waiter); resolve(value); };
            const waiter = () => { if (this.#closed) return finish(undefined); const evidence = match(); if (evidence) finish(evidence); };
            const timer = setTimeout(() => finish(undefined), timeoutMs);
            this.#waiters.add(waiter);
        });
    }
}
