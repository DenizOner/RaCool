let sequence = 0;
// Correlate bridge replies; late/unrelated responses cannot finish this request.
export function bridgeRequest(events, publish, base, command, payload, timeoutMs = 15000) {
    const transaction = 'racool-' + Date.now() + '-' + (++sequence);
    return new Promise((resolve, reject) => {
        const responseTopic = base + '/bridge/response/' + command;
        const cleanup = () => {clearTimeout(timer); events.off('message', receive); events.off('transport_closed', closed);};
        const closed = () => {cleanup(); reject(Error('Bridge connection closed; request delivery may be unknown'));};
        const receive = (topic, response) => {
            if (topic !== responseTopic || response.transaction !== transaction) return;
            cleanup();
            // Broker-provided error text is untrusted and may contain credentials,
            // configuration or encoded backup data. Never propagate it into
            // diagnostic console messages or persisted JSON artifacts.
            if (response.status !== 'ok') reject(Error('Bridge request rejected; remote error details redacted'));
            else resolve({transaction, timestamp: Date.now(), data: response.data});
        };
        const timer = setTimeout(() => {cleanup(); reject(Error('Bridge request timeout: ' + command));}, timeoutMs);
        events.on('message', receive);
        events.on('transport_closed', closed);
        try {publish(base + '/bridge/request/' + command, {...payload, transaction});}
        catch (error) {cleanup(); reject(error);}
    });
}
