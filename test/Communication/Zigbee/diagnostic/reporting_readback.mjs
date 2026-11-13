import assert from 'node:assert/strict';

/** Validate the correlated device response, not the bridge's cached metadata. */
export function validateReportingReadback(data, endpoint, cluster, interval) {
    assert.equal(data?.endpoint, endpoint, 'Reporting endpoint mismatch');
    assert.equal(data?.cluster, cluster, 'Reporting cluster mismatch');
    const attribute = cluster === 'msTemperatureMeasurement' ? 0 : 0x55;
    const configs = data.configs?.filter(entry => entry.attrId === attribute && entry.direction === 0);
    assert.equal(configs?.length, 1, 'Missing or ambiguous reporting result');
    const config = configs[0];
    assert.equal(config.status, 0, 'Device rejected reporting read');
    assert.equal(config.maxRepIntval, interval === 0 ? 0xffff : interval,
        'Device reporting interval differs from requested state');
    assert.ok(Number.isInteger(config.minRepIntval) && config.minRepIntval >= 0 &&
        config.minRepIntval <= config.maxRepIntval, 'Invalid reporting minimum');
    return config;
}
