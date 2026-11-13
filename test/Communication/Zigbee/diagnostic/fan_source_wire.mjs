const types = Object.fromEntries(Array.from({length:18},(_,index)=>[0x8050+index,
    index>=3 && index<=10 ? 0x29 : 0x20]));

export function fanSourceObservationDuration(value) {
    if (value === undefined) return 22000;
    const duration = Number(value);
    if (!Number.isInteger(duration) || duration < 22000 || duration > 60000)
        throw Error('Fan source observation must be an integer from 22000 to 60000 ms');
    return duration;
}

export function fanSourceReadRequest(device, attributes=Object.keys(types).map(Number)) {
    if (!/^0x[0-9a-f]{16}$/i.test(device.ieee_address) || !Number.isInteger(device.network_address) ||
        device.network_address<0 || device.network_address>=0xfff8 || !attributes.length ||
        new Set(attributes).size!==attributes.length || attributes.some(id=>!Object.hasOwn(types,id)))
        throw Error('Invalid fan source read target');
    return {action:'raw',params:{ieee_address:device.ieee_address,network_address:device.network_address,
        dst_endpoint:1,cluster_key:'hvacUserInterfaceCfg',timeout:20000,disable_response:false,
        zcl:{frame_type:0,direction:0,disable_default_response:false,command_key:'read',payload:attributes.map(attrId=>({attrId}))}}};
}

export function fanSourceReadResponse(data,device,attributes=Object.keys(types).map(Number)) {
    fanSourceReadRequest(device,attributes);
    if (data?.clusterID!==0x0204 || data.endpoint!==1 || data.address!==device.network_address ||
        data.data?.type!=='Buffer' || !Array.isArray(data.data.data) ||
        data.data.data.some(value=>!Number.isInteger(value)||value<0||value>255)) throw Error('Missing fan source wire response');
    const bytes=Buffer.from(data.data.data),values={};
    if(bytes.length<3 || (bytes[0]&15)!==8 || bytes[2]!==1) throw Error('Invalid fan source response header');
    let offset=3;
    while(offset<bytes.length) {
        if(offset+5>bytes.length) throw Error('Truncated fan source attribute');
        const id=bytes.readUInt16LE(offset),type=bytes[offset+3],length=type===0x29?2:1;
        if(!attributes.includes(id)||Object.hasOwn(values,id)||bytes[offset+2]!==0||type!==types[id]||offset+4+length>bytes.length)
            throw Error('Failed or unexpected fan source attribute');
        values[id]=length===2?bytes.readInt16LE(offset+4):bytes[offset+4];offset+=4+length;
    }
    if(Object.keys(values).length!==attributes.length) throw Error('Incomplete fan source read');
    return values;
}
