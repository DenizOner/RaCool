"""Validate captured real SDK startup ZCL evidence; never issue commands."""
import argparse
import json
import struct
from pathlib import Path


def validate(document, boot):
    def require(condition, message):
        if not condition: raise ValueError(message)
    require(boot.get('completed') and boot.get('diagnostic_marker') and boot.get('command_count')==1 and boot.get('settings_mutated') is False,'Missing fresh controlled software boot')
    require(document.get('completed') and not document.get('errors'),'Incomplete wire reader or bridge errors')
    require(all(reply.get('status')=='ok' for reply in document.get('responses',[])),'Rejected bridge request')
    transactions=set();counts={5:0,6:0};timeouts=0
    for record in document.get('startupWire',[]):
        if 'error' in record:
            require(record['error']=='Bridge request timeout: action','Unexpected reader error')
            timeouts+=1
            continue
        endpoint=record['endpoint'];require(endpoint in counts,'Unexpected endpoint')
        reply=record['reply'];wire=reply['data'];transaction=reply['transaction']
        require(transaction not in transactions,'Duplicate transaction');transactions.add(transaction)
        require(reply['timestamp']>=record['issued']>boot['join_ms'],'Invalid request/boot timeline')
        require(0<=record['issued']-boot['rom_ms']<60000,'Request outside diagnostic hold window')
        require(wire['address']==document['device']['network_address'] and wire['endpoint']==endpoint,'Wrong response target')
        require(wire['clusterID']==(0x402 if endpoint==5 else 0x0c),'Wrong response cluster')
        require(wire['data']['type']=='Buffer','Missing raw ZCL bytes')
        raw=bytes(wire['data']['data']);attribute=0 if endpoint==5 else 0x55;data_type=0x29 if endpoint==5 else 0x39
        require(len(raw)==(9 if endpoint==5 else 11),'Invalid ZCL record length')
        require(raw[0]&15==8 and raw[2]==1 and raw[1]==wire['header']['transactionSequenceNumber'],'Wrong ZCL response header')
        require(int.from_bytes(raw[3:5],'little')==attribute and raw[5]==0 and raw[6]==data_type,'Wrong attribute/status/type')
        value=struct.unpack('<h' if endpoint==5 else '<f',raw[7:])[0]
        require(value==record['value']==(-32768 if endpoint==5 else -1),'Wrong invalid sentinel')
        counts[endpoint]+=1
    require(all(counts.values()),'Both endpoints require real raw read responses')
    return {'wire_validated':True,'software_boot_validated':True,'diagnostic_window':True,
            'cpu_invalid_reads':counts[5],'health_invalid_reads':counts[6],'initial_action_timeouts':timeouts,
            'normal_boot_window_duration_proven':False}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wire',required=True);parser.add_argument('--boot',required=True);parser.add_argument('--output',required=True)
    args=parser.parse_args()
    result=validate(json.loads(Path(args.wire).read_text(encoding='utf-8')),json.loads(Path(args.boot).read_text(encoding='utf-8')))
    Path(args.output).write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__':main()
