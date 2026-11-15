import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'diagnostic'))
from frame_evidence import parse_frame,bitmap,write_png
from tempfile import TemporaryDirectory
from PIL import Image, UnidentifiedImageError
import unittest

def records():
    pixels=bytearray(1024);pixels[0]=1;pixels[-1]=128;digest=2166136261
    for value in pixels:digest=((digest^value)*16777619)&0xffffffff
    lines=['RACOOL_CLI v=1 type=oled_frame_begin seq=7 t_ms=12 width=128 height=64 bytes=1024 powered=1 inverted=0 flipped=0']
    for offset in range(0,1024,64):lines.append(f'RACOOL_CLI v=1 type=oled_frame_data seq=7 offset={offset} hex={pixels[offset:offset+64].hex()}')
    lines.append(f'RACOOL_CLI v=1 type=oled_frame_end seq=7 fnv={digest:08x}')
    return '\n'.join(lines)+'\n'

class FrameEvidence(unittest.TestCase):
    def test_page_corner_mapping_and_power(self):
        frame=parse_frame(records());rows=bitmap(frame)
        self.assertEqual(255,rows[0][0]);self.assertEqual(255,rows[63][127]);self.assertEqual(0,rows[0][1])
        frame['powered']=False;self.assertEqual(0,bitmap(frame,visible=True)[0][0]);self.assertEqual(255,bitmap(frame)[0][0])
    def test_inversion_and_flip_are_applied(self):
        frame=parse_frame(records());frame['pixels']=bytes([1])+bytes(1023);frame['flipped']=True
        self.assertEqual(255,bitmap(frame)[63][127]);self.assertEqual(0,bitmap(frame)[0][0])
        frame['inverted']=True;self.assertEqual(0,bitmap(frame)[63][127])
    def test_missing_duplicate_mixed_or_corrupt_records_fail(self):
        valid=records()
        for bad in [valid.rsplit('\n',2)[0],valid+valid,valid.replace('offset=64','offset=0'),valid.replace('seq=7 offset=64','seq=8 offset=64'),valid.replace('fnv=','fnv=0'),valid.replace('hex=0100','hex=0000'),valid.replace('width=128','width=127')]:
            with self.assertRaises(ValueError):parse_frame(bad)

    def test_png_output_roundtrip_and_truncation_rejection(self):
        with TemporaryDirectory() as tmp:
            path=Path(tmp)/'oled.png'
            write_png(parse_frame(records()),path)
            with Image.open(path) as img:
                img.load()
                self.assertEqual((128,64),img.size)
                self.assertEqual('L',img.mode)
                self.assertEqual(255,img.getpixel((0,0)))
                self.assertEqual(255,img.getpixel((127,63)))
                self.assertEqual(0,img.getpixel((1,0)))
            truncated=Path(tmp)/'truncated.png'
            truncated.write_bytes(path.read_bytes()[:12])
            with self.assertRaises((UnidentifiedImageError,OSError)):
                with Image.open(truncated) as invalid:
                    invalid.load()

if __name__=='__main__':unittest.main()
