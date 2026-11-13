"""Decode the exact successful SSD1306 page payload exported by the OLED owner."""
import re
import struct
import zlib
from pathlib import Path


def parse_frame(text):
    text=re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    lines=[line.strip() for line in text.splitlines() if line.startswith('RACOOL_CLI v=1 type=oled_frame_')]
    if any('type=oled_frame_error' in line for line in lines):raise ValueError('OLED owner rejected frame capture')
    if len(lines)!=18:raise ValueError('Incomplete or duplicate frame records')
    begin=re.fullmatch(r'RACOOL_CLI v=1 type=oled_frame_begin seq=(\d+) t_ms=(\d+) width=128 height=64 bytes=1024 powered=([01]) inverted=([01]) flipped=([01])',lines[0])
    if not begin:raise ValueError('Invalid frame header')
    seq,timestamp,powered,inverted,flipped=map(int,begin.groups());pixels=bytearray()
    for offset,line in zip(range(0,1024,64),lines[1:-1]):
        match=re.fullmatch(r'RACOOL_CLI v=1 type=oled_frame_data seq=(\d+) offset=(\d+) hex=([0-9a-f]{128})',line)
        if not match or int(match[1])!=seq or int(match[2])!=offset:raise ValueError('Mixed sequence or invalid chunk order')
        pixels.extend(bytes.fromhex(match[3]))
    end=re.fullmatch(r'RACOOL_CLI v=1 type=oled_frame_end seq=(\d+) fnv=([0-9a-f]{8})',lines[-1])
    digest=2166136261
    for value in pixels:digest=((digest^value)*16777619)&0xffffffff
    if not end or int(end[1])!=seq or int(end[2],16)!=digest:raise ValueError('Incomplete or corrupted frame checksum')
    return {'sequence':seq,'timestamp_ms':timestamp,'powered':bool(powered),'inverted':bool(inverted),'flipped':bool(flipped),'pixels':bytes(pixels)}


def bitmap(frame,scale=1,visible=False):
    if not 1<=scale<=16:raise ValueError('Invalid scale')
    rows=[]
    for y in range(64):
        row=[]
        for x in range(128):
            sx,sy=(127-x,63-y) if frame['flipped'] else (x,y)
            on=bool(frame['pixels'][(sy//8)*128+sx]&(1<<(sy%8)))^frame['inverted']
            if visible and not frame['powered']:on=False
            row.extend([255 if on else 0]*scale)
        rows.extend([bytes(row)]*scale)
    return rows


def write_png(frame,path,scale=1,visible=False):
    rows=bitmap(frame,scale,visible)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    payload=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',128*scale,64*scale,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+row for row in rows)))+chunk(b'IEND',b'')
    Path(path).write_bytes(payload)
