export function validateZoneColor(value) {
    if (!value || !Number.isInteger(value.x) || !Number.isInteger(value.y) ||
        value.x < 0 || value.x > 65279 || value.y < 1 || value.y > 65279) throw Error('Invalid zone XY');
    return value;
}
export function normalizedZoneColor(value) {
    validateZoneColor(value);
    if (Array.isArray(value) || Object.keys(value).length !== 2) throw Error('Invalid zone XY shape');
    return {x:value.x / 65536,y:value.y / 65536};
}
export function zoneColorRequest(device, zone) {
    if (!/^0x[0-9a-f]{16}$/i.test(device.ieee_address) || !Number.isInteger(device.network_address) ||
        device.network_address < 0 || device.network_address >= 0xfff8 || !Number.isInteger(zone) || zone < 0 || zone > 4)
        throw Error('Invalid zone read target');
    return {action: 'raw', params: {ieee_address: device.ieee_address, network_address: device.network_address,
        dst_endpoint: 3, cluster_key: 'genOnOff', timeout: 20000, disable_response: false,
        zcl: {frame_type: 0, direction: 0, disable_default_response: false,
            command_key: 'read', payload: [{attrId: 0x8030 + zone}]}}};
}
export function zoneColorResponse(data, device, zone) {
    zoneColorRequest(device, zone);
    if (data?.clusterID !== 6 || data.endpoint !== 3 || data.address !== device.network_address ||
        data.data?.type !== 'Buffer' || !Array.isArray(data.data.data) ||
        data.data.data.some(value => !Number.isInteger(value) || value < 0 || value > 255)) throw Error('Missing zone wire response');
    const bytes = Buffer.from(data.data.data);
    if (bytes.length !== 11 || (bytes[0] & 3) !== 0 || !(bytes[0] & 8) || (bytes[0] & 4) || bytes[2] !== 1 ||
        bytes.readUInt16LE(3) !== 0x8030 + zone || bytes[5] !== 0 || bytes[6] !== 0x23) throw Error('Failed zone read response');
    return validateZoneColor({x: bytes.readUInt16LE(7), y: bytes.readUInt16LE(9)});
}

export function rangeResponse(data, device) {
    zoneColorRequest(device, 0);
    if (data?.clusterID !== 6 || data.endpoint !== 3 || data.address !== device.network_address ||
        data.data?.type !== 'Buffer' || !Array.isArray(data.data.data) ||
        data.data.data.some(value => !Number.isInteger(value) || value < 0 || value > 255)) throw Error('Missing range wire response');
    const bytes = Buffer.from(data.data.data), values = {};
    if (bytes.length !== 39 || (bytes[0] & 3) !== 0 || !(bytes[0] & 8) || (bytes[0] & 4) || bytes[2] !== 1) throw Error('Invalid range response header');
    for (let offset = 3; offset < bytes.length; offset += 6) {
        const id = bytes.readUInt16LE(offset);
        if (id < 0x8020 || id > 0x8025 || Object.hasOwn(values, id) || bytes[offset + 2] !== 0 || bytes[offset + 3] !== 0x29) throw Error('Failed range attribute');
        values[id] = bytes.readInt16LE(offset + 4) / 100;
    }
    return [0x8020, 0x8022, 0x8024].map(id => [values[id], values[id + 1]]);
}
