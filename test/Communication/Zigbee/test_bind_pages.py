import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / 'diagnostic'))
from analyze_bind_pages import analyze


class BindingPageTests(unittest.TestCase):
    def test_complete_colored_pages(self):
        text = ('\x1b[32mI RPTBIND: error=0 status=0 total=2 start=0 count=1\x1b[0m\n'
                'I RPTBIND: index=0 source_ep=1 cluster=0x0006 destination_mode=3 destination_ep=1\n'
                'I RPTBIND: error=0 status=0 total=2 start=1 count=1\n'
                'I RPTBIND: index=1 source_ep=1 cluster=0x0204 destination_mode=3 destination_ep=1\n')
        result = analyze(text)
        self.assertTrue(result['complete'])
        self.assertEqual(result['entries'], 2)
        self.assertEqual(result['duplicate_source_cluster_pairs'], 0)

    def test_rejects_incomplete_duplicate_wrong_status_and_gaps(self):
        header = 'RPTBIND: error=0 status=0 total=2 start=0 count=1\n'
        entry = 'RPTBIND: index=0 source_ep=1 cluster=0x0006 destination_mode=3 destination_ep=1\n'
        for text in ('', header, header + entry, header + entry + entry,
                     'RPTBIND: error=0 status=1 total=0 start=0 count=0\n',
                     header + entry + 'RPTBIND: error=0 status=0 total=2 start=2 count=0\n'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                analyze(text)

    def test_empty_table_and_duplicate_source_cluster(self):
        self.assertEqual(analyze('RPTBIND: error=0 status=0 total=0 start=0 count=0\n')['entries'], 0)
        result = analyze('RPTBIND: error=0 status=0 total=2 start=0 count=2\n'
                         'RPTBIND: index=0 source_ep=1 cluster=0x0006 destination_mode=3 destination_ep=1\n'
                         'RPTBIND: index=1 source_ep=1 cluster=0x0006 destination_mode=3 destination_ep=1\n')
        self.assertEqual(result['duplicate_source_cluster_pairs'], 1)


if __name__ == '__main__':
    unittest.main()
