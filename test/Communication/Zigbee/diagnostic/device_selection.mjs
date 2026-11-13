export function normalizeDeviceIEEE(value) {
    if (value === undefined || value === null) return undefined;
    if (typeof value !== 'string' || !/^0x[0-9a-f]{16}$/i.test(value))
        throw new Error('RaCool device selector must be a full IEEE address');
    return value.toLowerCase();
}

export function selectRaCoolDevice(devices, selector) {
    const target = normalizeDeviceIEEE(selector);
    if (!Array.isArray(devices)) throw new Error('Invalid bridge device inventory');
    const candidates = devices.filter(device => device &&
        (device.definition?.model === 'RaCool-ESP32C6' || device.model_id === 'RaCool-ESP32C6'));
    const addresses = candidates.map(device => normalizeDeviceIEEE(device.ieee_address));
    if (addresses.some(address => !address) || new Set(addresses).size !== addresses.length)
        throw new Error('Missing or duplicate RaCool identity');
    if (target) {
        const selected = candidates.find((device, index) => addresses[index] === target);
        if (!selected) throw new Error('Selected RaCool device is absent from bridge inventory');
        return selected;
    }
    if (candidates.length > 1) throw new Error('Multiple RaCool devices: configure RACOOL_DEVICE_IEEE or deviceIEEE before any operation');
    return candidates[0];
}
