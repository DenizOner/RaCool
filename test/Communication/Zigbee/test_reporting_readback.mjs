import {test} from 'node:test';
import assert from 'node:assert/strict';
import {validateReportingReadback} from './diagnostic/reporting_readback.mjs';

const reply = (maximum = 65535) => ({endpoint:1, cluster:'msTemperatureMeasurement',
    configs:[{attrId:0,direction:0,status:0,minRepIntval:1,maxRepIntval:maximum}]});

test('accepts explicit disabled and enabled device reporting state', () => {
    validateReportingReadback(reply(), 1, 'msTemperatureMeasurement', 0);
    validateReportingReadback(reply(1), 1, 'msTemperatureMeasurement', 1);
    validateReportingReadback({endpoint:8,cluster:'genAnalogInput',
        configs:[{attrId:85,direction:0,status:0,minRepIntval:1,maxRepIntval:600}]},
        8,'genAnalogInput',600);
});
test('rejects stale intervals and protocol errors', () => {
    for (const value of [reply(3), {...reply(),endpoint:4}, {...reply(),cluster:'genAnalogInput'},
        {...reply(),configs:[]}, {...reply(),configs:[{...reply().configs[0],status:134}]},
        {...reply(),configs:[...reply().configs,...reply().configs]}]) {
        assert.throws(() => validateReportingReadback(value,1,'msTemperatureMeasurement',0));
    }
});
