import {test} from 'node:test';
import assert from 'node:assert/strict';
import {buildReportingChangePlan, changedCalibration, measuredReadResponses, validateChangedMeasurement} from './diagnostic/reporting_changes.mjs';
const trigger = {name:'radiator_temperature_calibration', property:'calibration', type:'numeric', access:7, value_min:-10, value_max:10};
const measurement = {name:'temperature', property:'temperature_fan', endpoint:'climate', access:5};
const report = {name:'report_interval_radiator_s',property:'report_fan'};
test('resolves the complete trigger selection before writes and rejects unsupported routes', () => {
    assert.equal(buildReportingChangePlan([trigger,measurement],[report])[0].endpoint,1);
    assert.throws(()=>buildReportingChangePlan([trigger,measurement],[report,{name:'unknown_report'}]),/No controlled/);
    assert.throws(()=>buildReportingChangePlan([trigger],[report]),/Missing/);
});
test('selects a bounded changed calibration without accepting stale invalid values', () => {
    assert.equal(changedCalibration(trigger,0),1);
    assert.equal(changedCalibration(trigger,10),9);
    assert.throws(()=>changedCalibration(trigger,NaN));
    assert.throws(()=>changedCalibration(trigger,11));
});
test('accepts actual changed ZCL reads while excluding retained state and other routes', () => {
    const line = "Received Zigbee message from 'RaCool', type 'readResponse', cluster 'msTemperatureMeasurement', data '{\"measuredValue\":2500}' from endpoint 1 with groupID 0";
    const text = [line,line.replace('readResponse','attributeReport'),line.replace('endpoint 1 ','endpoint 10 '),line.replace('RaCool','Other'),line.replace('2500','2600')].join('\n');
    const values=measuredReadResponses(text,'RaCool',1,'msTemperatureMeasurement','measuredValue');
    assert.deepEqual(values,[2500,2600]); validateChangedMeasurement(values);
    assert.throws(()=>validateChangedMeasurement([2500,2500]));
    assert.throws(()=>validateChangedMeasurement([2500]));
    assert.throws(()=>measuredReadResponses(line.replace('2500','null'),'RaCool',1,'msTemperatureMeasurement','measuredValue'));
});

test('uptime change requires a readable measurement and no mutable trigger', () => {
    const uptime = {name:'uptime_seconds',property:'uptime_seconds',access:5};
    const report = {name:'report_interval_uptime_s',property:'report_uptime'};
    const plan = buildReportingChangePlan([uptime],[report])[0];
    assert.equal(plan.kind,'elapsed'); assert.equal(plan.endpoint,8);
    assert.equal(plan.trigger,undefined);
    assert.throws(()=>buildReportingChangePlan([{...uptime,access:1}],[report]),/readable uptime/);
});

test('RAM change uses the owned trace allocation and validates cleanup evidence', async () => {
    const {validateRamCapture} = await import('./diagnostic/trace_ram_capture.mjs');
    const plan=buildReportingChangePlan([{name:'free_memory_percent',property:'free_memory_percent',access:5}],
        [{name:'report_interval_memory_s',property:'report_ram'}])[0];
    assert.equal(plan.kind,'trace_ram'); assert.equal(plan.endpoint,7);
    const valid={completed:true,trace_disabled:true,initial_trace:{enabled:false},summary:{dropped:0,samples:20,rows:8}};
    assert.equal(validateRamCapture(valid).traceDisabled,true);
    for (const invalid of [{...valid,completed:false},{...valid,trace_disabled:false},
        {...valid,initial_trace:{enabled:true}},{...valid,summary:{...valid.summary,dropped:1}}])
        assert.throws(()=>validateRamCapture(invalid));
});

test('CPU change uses guarded local calibration and requires completed restoration', async () => {
    const {validateCpuCapture}=await import('../../Hardware/InternalTemperatureSensor/diagnostic/calibration_capture.mjs');
    const measurement={name:'temperature',endpoint:'processor_sensor',property:'cpu',access:5};
    const plan=buildReportingChangePlan([measurement],[{name:'report_interval_processor_s',property:'report_cpu'}])[0];
    assert.equal(plan.kind,'cpu_calibration'); assert.equal(plan.trigger,undefined);
    assert.throws(()=>buildReportingChangePlan([],[{name:'report_interval_processor_s'}]),/Missing/);
    const valid={completed:true,restoration_verified:true,original_offset:0,requested_offset:1};
    assert.equal(validateCpuCapture(valid),valid);
    for (const invalid of [{...valid,completed:false},{...valid,restoration_verified:false},
        {...valid,requested_offset:0},{...valid,original_offset:NaN}]) assert.throws(()=>validateCpuCapture(invalid));
});

test('RAM evidence requires a substantial directed allocation decrease', () => {
    validateChangedMeasurement([28,15],5);
    assert.throws(()=>validateChangedMeasurement([13,12.9],5));
    assert.throws(()=>validateChangedMeasurement([15,28],5));
});

test('real health change requires readable health and completed service restoration', async () => {
    const {validateHealthCapture}=await import('./diagnostic/health_capture.mjs');
    const plan=buildReportingChangePlan([{name:'system_health',access:5}],[{name:'report_interval_health_s'}])[0];
    assert.equal(plan.endpoint,6); assert.equal(plan.kind,'real_service_health');
    const valid={completed:true,restoration_verified:true,initial_score:100,changed_score:88};
    assert.equal(validateHealthCapture(valid),valid);
    for(const invalid of [{...valid,completed:false},{...valid,restoration_verified:false},{...valid,changed_score:100}]) assert.throws(()=>validateHealthCapture(invalid));
    assert.throws(()=>buildReportingChangePlan([{name:'system_health',access:1}],[{name:'report_interval_health_s'}]),/Missing/);
});
