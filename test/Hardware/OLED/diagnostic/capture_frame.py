"""Capture a checked OLED frame without waking or redrawing the display."""
import argparse
import codecs
import json
import time
import sys
from pathlib import Path
import serial
from frame_evidence import parse_frame,write_png
sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession, parse_mode

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--output',required=True)
    args=parser.parse_args();prefix=Path(args.output);prefix.parent.mkdir(parents=True,exist_ok=True)
    port=serial.Serial(port=None,baudrate=115200,timeout=.1);port.dtr=port.rts=False;port.port=args.port
    with port,prefix.with_suffix('.log').open('w',encoding='utf-8') as log:
        port.reset_input_buffer()
        def exchange(command):
            port.write((command+'\r\n').encode()); response=''; deadline=time.monotonic()+8; matched_at=None
            while time.monotonic()<deadline:
                chunk=port.read(port.in_waiting or 1).decode('utf-8',errors='replace')
                response+=chunk; log.write(chunk); log.flush()
                try:
                    parse_mode(response)
                    if matched_at is None: matched_at=time.monotonic()
                except RuntimeError:
                    if matched_at is not None: raise
                if matched_at is not None and time.monotonic()-matched_at>=0.25: return response
            raise RuntimeError('Developer Mode response timed out or was ambiguous')
        session=DeveloperSession(exchange)
        try:
            session.open()
            port.write(b'oled frame --machine\r\n');text='';decoder=codecs.getincrementaldecoder('utf-8')('replace');deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                chunk=decoder.decode(port.read(port.in_waiting or 1));text+=chunk;log.write(chunk);log.flush()
                if 'type=oled_frame_error' in text:raise RuntimeError('Owner frame capture failed')
                if re_end(text):break
            frame=parse_frame(text)
        finally:
            session.close()
    metadata={key:value for key,value in frame.items() if key!='pixels'}
    metadata['evidence']='successful_sdk_page_payload_not_optical_measurement'
    prefix.with_suffix('.json').write_text(json.dumps(metadata,indent=2),encoding='utf-8')
    prefix.with_suffix('.bin').write_bytes(frame['pixels'])
    write_png(frame,prefix.with_suffix('.png'),visible=True)
    write_png(frame,prefix.with_name(prefix.name+'-ram').with_suffix('.png'))
    write_png(frame,prefix.with_name(prefix.name+'-8x').with_suffix('.png'),8,visible=True)
    print(json.dumps(metadata))

def re_end(text):
    import re
    return re.search(r'^RACOOL_CLI v=1 type=oled_frame_end seq=\d+ fnv=[0-9a-f]{8}\r*\n',text,re.M)

if __name__=='__main__':main()
