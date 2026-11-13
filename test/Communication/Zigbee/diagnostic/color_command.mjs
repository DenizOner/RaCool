export function colorRequest(device, coordinates) {
    if (!/^0x[0-9a-f]{16}$/i.test(device.ieee_address) || !Number.isInteger(device.network_address) ||
        device.network_address < 0 || device.network_address >= 0xfff8) throw Error('Invalid unicast color target');
    const params={ieee_address:device.ieee_address,network_address:device.network_address,
        dst_endpoint:3,cluster_key:'lightingColorCtrl',timeout:20000,disable_response:false,
        zcl:{frame_type:coordinates ? 1 : 0,direction:0,disable_default_response:false,
            command_key:coordinates ? 'moveToColor' : 'read',payload:coordinates ?
                {colorx:coordinates.x,colory:coordinates.y,transtime:0,optionsMask:1,optionsOverride:1} :
                [{attrId:3},{attrId:4}]}};
    if (coordinates && (!Number.isInteger(coordinates.x) || !Number.isInteger(coordinates.y) ||
        coordinates.x < 0 || coordinates.x > 0xfeff || coordinates.y <= 0 || coordinates.y > 0xfeff))
        throw Error('Invalid XY coordinates');
    return {action:'raw',params};
}
export function colorResponse(data, device, command) {
    if (data?.clusterID !== 0x300 || data.endpoint !== 3 || data.address !== device.network_address ||
        data.data?.type !== 'Buffer' || !Array.isArray(data.data.data) ||
        data.data.data.some(value=>!Number.isInteger(value)||value<0||value>255))
        throw Error('Missing matching wire color response');
    const bytes=Buffer.from(data.data.data);
    if (bytes.length < 3 || (bytes[0]&3)!==0 || !(bytes[0]&8) || (bytes[0]&4))
        throw Error('Invalid server ZCL response header');
    if (command === 'moveToColor') {
        if (bytes.length !== 5 || bytes[2] !== 11 || bytes[3] !== 7 || bytes[4] !== 0)
            throw Error('MoveToColor default response did not succeed');
        return {status:0};
    }
    if (bytes[2] !== 1) throw Error('Missing color read response');
    const values={};
    for (let offset=3;offset<bytes.length;offset+=6) {
        if (offset+6>bytes.length || bytes[offset+2]!==0 || bytes[offset+3]!==0x21)
            throw Error('Incomplete or failed XY attribute response');
        const id=bytes.readUInt16LE(offset);
        if (![3,4].includes(id) || Object.hasOwn(values,id)) throw Error('Unexpected or duplicate color attribute');
        values[id]=bytes.readUInt16LE(offset+4);
    }
    if (!Object.hasOwn(values,3)||!Object.hasOwn(values,4)) throw Error('Missing coordinate attribute');
    return {x:values[3],y:values[4]};
}
