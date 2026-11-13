import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {readNewLog} from './diagnostic/reporting_traffic.mjs';
import {selectAttributeReports} from './diagnostic/reporting_traffic.mjs';

test('counts only unsolicited attribute reports from the selected device and route', () => {
    const line = "debug: z2m: Received Zigbee message from 'test device', type 'attributeReport', cluster 'genAnalogInput', data '{}' from endpoint 8 with groupID 0";
    const input = [line, line.replace('attributeReport','readResponse'),
        line.replace('test device','other device'), line.replace('endpoint 8 ','endpoint 80 '),
        line.replace('genAnalogInput','msTemperatureMeasurement')].join('\n');
    assert.deepEqual(selectAttributeReports(input,'test device',8,'genAnalogInput'), [line]);
});

test('preserves an observation across numbered bridge log rotation', async () => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'racool-report-log-'));
    try {
        await fs.writeFile(path.join(directory, 'log2.log'), 'old report\n');
        await fs.writeFile(path.join(directory, 'log1.log'), 'request start-id\nreply start-id\nfirst report\n');
        await fs.writeFile(path.join(directory, 'log.log'), 'second report\nrequest end-id\nreply end-id\n');
        await fs.writeFile(path.join(directory, 'unrelated.log'), 'end-id\nstart-id\n');
        assert.equal(await readNewLog({file:path.join(directory, 'log.log')}, 'start-id', 'end-id'),
            'first report\n\nsecond report\nrequest ');
        await fs.unlink(path.join(directory, 'log1.log'));
        await assert.rejects(readNewLog({file:path.join(directory, 'log.log')}, 'start-id', 'end-id'), /markers/);
    } finally {
        await fs.rm(directory, {recursive:true, force:true});
    }
});

test('uses correlated log markers instead of stale mapped-file size or wall clocks', async () => {
    const {selectResponseWindow} = await import('./diagnostic/reporting_traffic.mjs');
    assert.equal(selectResponseWindow('old report\nrequest start-id\nreply start-id\nfresh report\nrequest end-id\nreply end-id\n','start-id','end-id'),'fresh report\nrequest ');
    assert.throws(() => selectResponseWindow('cached log','start-id','end-id'));
    assert.throws(() => selectResponseWindow('end-id\nstart-id\n','start-id','end-id'));
});
