/** Only an explicitly identified other device's failed route is outside this device test. */
export function isUnrelatedDeviceRouteError(message, device) {
    if (typeof message !== 'string' || !device?.ieee_address || !device?.friendly_name) return false;
    if (!message.includes("Data request failed with error: 'NWK_NO_ROUTE'")) return false;
    const name = message.match(/Failed to configure '([^']+)'/i)?.[1];
    const ieee = message.match(/ZCL command (0x[0-9a-f]{16})\//i)?.[1];
    if (!name || !ieee) return false;
    if (message.toLowerCase().includes(device.ieee_address.toLowerCase()) ||
        message.includes(device.friendly_name)) return false;
    if (/inter-pan|adapter disconnect|coordinator|mqtt/i.test(message)) return false;
    return name !== device.friendly_name && ieee.toLowerCase() !== device.ieee_address.toLowerCase();
}
