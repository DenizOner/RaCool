const kinds=['radiator','ambient','processor','health','memory','uptime'];
const endpoints=[1,4,5,6,7,8];
const suffixes=['climate','ambient_sensor','processor_sensor','system_health','memory','uptime'];

export function reportIntervalPair(control,property) {
    const index=kinds.findIndex(name=>control==='report_interval_'+name+'_s');
    if(index<0 || property!==control+'_'+suffixes[index]) throw Error('Invalid reporting interval control/property pair');
    return {endpoint:endpoints[index],attribute:0x8030+index};
}
function target(device,control) {
    const index=kinds.findIndex(name=>control==='report_interval_'+name+'_s');
    if (!device || !/^0x[0-9a-f]{16}$/i.test(device.ieee_address) ||
        !Number.isInteger(device.network_address) || device.network_address<0 ||
        device.network_address>=0xfff8 || index<0) throw Error('Invalid reporting interval target');
    return {endpoint:endpoints[index],attribute:0x8030+index};
}

export function reportIntervalRequest(device,control) {
    const {endpoint,attribute}=target(device,control);
    return {action:'raw',params:{ieee_address:device.ieee_address,network_address:device.network_address,
        dst_endpoint:endpoint,cluster_key:'hvacUserInterfaceCfg',timeout:20000,disable_response:false,
        zcl:{frame_type:0,direction:0,disable_default_response:false,command_key:'read',payload:[{attrId:attribute}]}}};
}

export function reportIntervalResponse(data,device,control) {
    const {endpoint,attribute}=target(device,control);
    if(data?.clusterID!==0x0204 || data.endpoint!==endpoint || data.address!==device.network_address ||
        data.data?.type!=='Buffer' || !Array.isArray(data.data.data) ||
        data.data.data.some(x=>!Number.isInteger(x)||x<0||x>255)) throw Error('Invalid reporting interval wire response');
    const bytes=Buffer.from(data.data.data);
    if(bytes.length!==9 || (bytes[0]&15)!==8 || bytes[2]!==1 || bytes.readUInt16LE(3)!==attribute ||
        bytes[5]!==0 || bytes[6]!==0x21) throw Error('Failed reporting interval read');
    const value=bytes.readUInt16LE(7);
    if(value>600) throw Error('Reporting interval is outside its allowed range');
    return value;
}
