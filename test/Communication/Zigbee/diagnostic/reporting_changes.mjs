import assert from 'node:assert/strict';

export function buildReportingChangePlan(exposes, controls) {
    const all = exposes.flatMap(e => [e, ...(e.features ?? [])]);
    const mapping = {
        report_interval_radiator_s: ['radiator_temperature_calibration', 'climate', 1],
        report_interval_ambient_s: ['ambient_temperature_calibration', 'ambient_sensor', 4],
    };
    return controls.map(report => {
        if (report.name === 'report_interval_health_s') {
            const measurement = all.find(e => e.name === 'system_health');
            if (!measurement || !(measurement.access & 4)) throw Error('Missing readable health measurement');
            return {reportProperty:report.property, endpoint:6, kind:'real_service_health', measurement};
        }
        if (report.name === 'report_interval_processor_s') {
            const measurement = all.find(e => e.name === 'temperature' && e.endpoint === 'processor_sensor');
            if (!measurement || !(measurement.access & 4)) throw Error('Missing readable CPU measurement');
            return {reportProperty: report.property, endpoint:5, kind:'cpu_calibration', measurement};
        }
        if (['report_interval_uptime_s','report_interval_memory_s'].includes(report.name)) {
            const ram = report.name === 'report_interval_memory_s';
            const measurement = all.find(e => e.name === (ram ? 'free_memory_percent' : 'uptime_seconds'));
            if (!measurement || !(measurement.access & 4)) throw Error('Missing readable uptime measurement');
            return {reportProperty: report.property, endpoint: ram ? 7 : 8, kind: ram ? 'trace_ram' : 'elapsed', measurement};
        }
        const route = mapping[report.name];
        if (!route) throw Error('No controlled change trigger implemented for ' + report.name);
        const trigger = all.find(e => e.name === route[0]);
        const measurement = all.find(e => e.name === 'temperature' && e.endpoint === route[1]);
        if (!trigger || trigger.type !== 'numeric' || (trigger.access & 6) !== 6 ||
            !Number.isFinite(trigger.value_min) || !Number.isFinite(trigger.value_max) ||
            trigger.value_max - trigger.value_min < 1 || !measurement || !(measurement.access & 4))
            throw Error('Missing readable measurement or writable calibration trigger');
        return {reportProperty: report.property, endpoint: route[2], trigger, measurement};
    });
}

export function changedCalibration(trigger, original) {
    if (!Number.isFinite(original) || original < trigger.value_min || original > trigger.value_max)
        throw Error('Invalid initial calibration');
    return Number((original + 1 <= trigger.value_max ? original + 1 : original - 1).toFixed(2));
}

export function measuredReadResponses(text, device, endpoint, cluster, attribute) {
    const values = [];
    for (const line of text.split(/\r?\n/)) {
        if (!line.includes("Received Zigbee message from '" + device + "'") ||
            !line.includes("type 'readResponse'") || !line.includes("cluster '" + cluster + "'") ||
            !line.includes('from endpoint ' + endpoint + ' ')) continue;
        const match = /data '(.*?)' from endpoint/.exec(line);
        if (!match) throw Error('Incomplete measured ZCL read response');
        const data = JSON.parse(match[1]);
        if (Object.hasOwn(data, attribute)) {
            if (!Number.isFinite(data[attribute])) throw Error('Invalid measured ZCL read value');
            values.push(data[attribute]);
        }
    }
    return values;
}

export function validateChangedMeasurement(values, minimumDecrease = 0) {
    assert.ok(values.length >= 2, 'Missing fresh measured ZCL responses around the trigger');
    assert.notEqual(values[0], values.at(-1), 'Measured attribute did not change during the controlled trigger');
    if (minimumDecrease > 0) assert.ok(values[0] - values.at(-1) >= minimumDecrease,
        'Measured decrease does not establish the controlled RAM allocation');
}

export function requestedChangeValue(plan, original) {
    return changedCalibration(plan.trigger, original);
}
