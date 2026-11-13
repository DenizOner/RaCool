import copy
import struct
import unittest
from diagnostic.startup_sentinel_evidence import validate

class StartupSentinelEvidence(unittest.TestCase):
    def setUp(self):
        self.boot={'completed':True,'diagnostic_marker':True,'command_count':1,'settings_mutated':False,'rom_ms':1000,'join_ms':2000}
        self.document={'completed':True,'device':{'network_address':123},'startupWire':[]}
        for endpoint,cluster,attribute,data_type,value in [(5,0x402,0,0x29,-32768),(6,0x0c,0x55,0x39,-1)]:
            raw=bytes([8,endpoint,1])+attribute.to_bytes(2,'little')+bytes([0,data_type])+struct.pack('<h' if endpoint==5 else '<f',value)
            self.document['startupWire'].append({'issued':3000,'endpoint':endpoint,'value':value,'reply':{'timestamp':3001,'transaction':str(endpoint),'data':{'address':123,'endpoint':endpoint,'clusterID':cluster,'data':{'type':'Buffer','data':list(raw)},'header':{'transactionSequenceNumber':endpoint}}}})
    def test_real_raw_records_required_for_both_endpoints(self):
        result=validate(self.document,self.boot)
        self.assertEqual(1,result['cpu_invalid_reads']);self.assertEqual(1,result['health_invalid_reads'])
        self.assertFalse(result['normal_boot_window_duration_proven'])
        self.document['startupWire'].pop()
        with self.assertRaises(ValueError):validate(self.document,self.boot)
    def test_wrong_target_type_value_or_sequence_is_rejected(self):
        for mutate in [lambda r:r['reply']['data'].update(address=124),lambda r:r['reply']['data']['data']['data'].__setitem__(6,0x39),lambda r:r.update(value=0),lambda r:r['reply']['data']['header'].update(transactionSequenceNumber=0)]:
            document=copy.deepcopy(self.document);mutate(document['startupWire'][0])
            with self.assertRaises(ValueError):validate(document,self.boot)
    def test_old_boot_or_duplicate_transaction_is_rejected(self):
        for mutate in [lambda d:d['startupWire'][0].update(issued=1999),lambda d:d['startupWire'][0].update(issued=61000),lambda d:d['startupWire'][1]['reply'].update(transaction='5')]:
            document=copy.deepcopy(self.document);mutate(document)
            with self.assertRaises(ValueError):validate(document,self.boot)
    def test_timeouts_are_explicit_and_bridge_errors_fail(self):
        self.document['startupWire'].append({'error':'Bridge request timeout: action'})
        self.assertEqual(1,validate(self.document,self.boot)['initial_action_timeouts'])
        self.document['errors']=['BUFFER_FULL']
        with self.assertRaises(ValueError):validate(self.document,self.boot)

if __name__=='__main__':unittest.main()
