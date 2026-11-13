"""Verify gallery admission, non-destructive navigation and checked restoration."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from Controllers.UIController.diagnostic import capture_oled_gallery as gallery

class Console:
    def __init__(self,state=0,corrupt=False,restore_mismatch=False,hidden=None):
        self.state=state;self.locale='tr';self.buffer=b'';self.commands=[]
        self.corrupt=corrupt;self.restore_mismatch=restore_mismatch;self.changed=False
        self.developer=False
        self.hidden=hidden
    def __enter__(self):return self
    def __exit__(self,*args):pass
    def reset_input_buffer(self):self.buffer=b''
    @property
    def in_waiting(self):return len(self.buffer)
    def read(self,n):data=self.buffer[:n];self.buffer=self.buffer[n:];return data
    def write(self,data):
        cmd=data.decode().strip();self.commands.append(cmd);prefix='RACOOL_CLI v=1 type='
        if cmd.startswith('DEVELOPER '):
            action=cmd.split()[1]
            if action=='ON':self.developer=True
            elif action=='OFF':self.developer=False
            line=f'developer_mode enabled={int(self.developer)}'
        elif cmd=='menu status --machine':line=f'menu_status state={self.state} registry_label_hex=74657374'
        elif cmd=='LANGUAGE CONFIG --machine':
            locale='en' if self.restore_mismatch and self.changed and self.locale=='tr' else self.locale
            line=f'language_config saved={locale} active={locale} restorable=1'
        elif cmd=='UI STATUS --machine':line='ui screen=active service=2 boot=idle'
        elif cmd=='UI WAKE --machine':line='ui_command command=WAKE result=queued error=0'
        elif cmd.startswith('LANGUAGE SET '):
            self.locale=cmd.split()[2];self.changed=True;line=f'language_set result=applied locale={self.locale}'
        elif cmd=='menu reset --machine':self.state=0;line='menu_navigation result=queued state=0'
        elif cmd.startswith('menu nav '):self.state=int(cmd.split()[2]);line=f'menu_navigation result=queued state={self.state}'
        elif cmd.startswith('menu visible '):
            target=int(cmd.split()[2]);line=f'menu_visibility state={target} visible={int(target!=self.hidden and target not in range(49,60))}'
        elif cmd=='menu click --machine':line='menu_input result=accepted input=button'
        elif cmd=='oled frame --machine':
            pixels=bytes(1024);digest=2166136261
            for value in pixels:digest=((digest^value)*16777619)&0xffffffff
            lines=[prefix+'oled_frame_begin seq=1 t_ms=1 width=128 height=64 bytes=1024 powered=1 inverted=0 flipped=0']
            lines.extend(prefix+f'oled_frame_data seq=1 offset={offset} hex={pixels[offset:offset+64].hex()}' for offset in range(0,1024,64))
            lines.append(prefix+f'oled_frame_end seq=1 fnv={0 if self.corrupt else digest:08x}')
            self.buffer=('\n'.join(lines)+'\n').encode();return
        else:raise AssertionError('Unexpected command: '+cmd)
        self.buffer=(prefix+line+'\n').encode()

class GallerySafety(unittest.TestCase):
    def test_explicit_hidden_policy_skips_without_navigation_or_click(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);console=Console(hidden=9)
            self.run_gallery(console,root)
            skipped=json.loads((root/'skipped.json').read_text())
            self.assertEqual(2,len([item for item in skipped if item['state']==9]));self.assertTrue(all(item['state']==9 or 49<=item['state']<60 for item in skipped))
            self.assertNotIn('menu nav 9 --machine',console.commands)
            self.assertEqual(212,len(json.loads((root/'frames.json').read_text())))
    def run_gallery(self,console,root):
        with patch.object(gallery.serial,'Serial',return_value=console),patch.object(gallery.time,'sleep'),patch.object(sys,'argv',['gallery','--port','MOCK','--output',str(root)]),contextlib.redirect_stdout(io.StringIO()):gallery.main()
    def test_complete_gallery_restores_and_never_confirms_actions(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);console=Console();self.run_gallery(console,root)
            self.assertTrue(json.loads((root/'restoration.json').read_text())['restored'])
            self.assertEqual('tr',console.locale);self.assertEqual(0,console.state)
            frames=json.loads((root/'frames.json').read_text())
            # Nested topology includes the menu auto-exit setting in both locales;
            # categories must never be captured by clicking them as editors.
            self.assertEqual(216,len(frames))
            labels={frame['label'] for frame in frames}
            for locale in ['tr','en']:
                self.assertIn(f'{locale}-ui_state_set_menu_auto_exit-editor',labels)
                for category in ['builtin_led','external_led','builtin_animations','rgb_regions',
                                 'zone_freeze','zone_cold','zone_warm','zone_hot','zone_danger',
                                 'screen','calibration','zigbee','energy','advanced','maintenance']:
                    self.assertIn(f'{locale}-ui_state_cat_{category}-list',labels)
                    self.assertNotIn(f'{locale}-ui_state_cat_{category}-editor',labels)
            ranges={
                'ui_state_set_high_temp':'ui_state_set_low_temp',
                'ui_state_set_rgb_temp_cold_max':'ui_state_set_rgb_temp_cold',
                'ui_state_set_rgb_temp_warm_max':'ui_state_set_rgb_temp_warm',
                'ui_state_set_rgb_temp_hot_max':'ui_state_set_rgb_temp_hot',
                'ui_state_fan_delta_on':'ui_state_fan_delta_off',
            }
            for locale in ['tr','en']:
                for upper,paired in ranges.items():
                    for view in ['list','editor']:
                        self.assertNotIn(f'{locale}-{upper}-{view}',labels)
                        self.assertIn(f'{locale}-{paired}-{view}',labels)
            for state in range(49,60):self.assertNotIn(f'menu nav {state} --machine',console.commands)
            clicked=False
            for cmd in console.commands:
                if cmd=='menu reset --machine':clicked=False
                if cmd=='menu click --machine':self.assertFalse(clicked);clicked=True
                self.assertFalse(cmd.startswith(('menu enc ','power ','CLIMATE ','config.')))
    def test_foreign_menu_is_refused_before_mutation(self):
        with tempfile.TemporaryDirectory() as folder:
            console=Console(state=5)
            with self.assertRaisesRegex(RuntimeError,'Foreign menu'):self.run_gallery(console,Path(folder))
            self.assertEqual(['menu status --machine'],[cmd for cmd in console.commands if not cmd.startswith('DEVELOPER ')])
            self.assertFalse(console.developer)
    def test_corrupted_frame_restores_locale_and_menu(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);console=Console(corrupt=True)
            with self.assertRaisesRegex(ValueError,'checksum'):self.run_gallery(console,root)
            self.assertTrue(json.loads((root/'restoration.json').read_text())['restored'])
            self.assertEqual(0,console.state);self.assertEqual('tr',console.locale)
    def test_restore_acknowledgment_without_readback_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            with self.assertRaisesRegex(RuntimeError,'restoration readback mismatch'):self.run_gallery(Console(corrupt=True,restore_mismatch=True),root)
            self.assertFalse((root/'restoration.json').exists())

if __name__=='__main__':unittest.main()
