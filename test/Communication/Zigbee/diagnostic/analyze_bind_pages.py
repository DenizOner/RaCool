"""Validate a complete, ordered SDK binding-table page capture."""
import collections
import json
import re
import sys
from pathlib import Path


def analyze(text):
    text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)
    pages = []
    for line in text.splitlines():
        header = re.search(r'RPTBIND: error=(-?\d+) status=(\d+) total=(\d+) start=(\d+) count=(\d+)', line)
        if header:
            error, status, total, start, count = map(int, header.groups())
            pages.append({'error': error, 'status': status, 'total': total, 'start': start, 'count': count, 'entries': []})
        entry = re.search(r'RPTBIND: index=(\d+) source_ep=(\d+) cluster=0x([0-9a-fA-F]+) destination_mode=(\d+) destination_ep=(\d+)', line)
        if entry:
            if not pages:
                raise ValueError('entry without page')
            index, endpoint, cluster, mode, dest = entry.groups()
            pages[-1]['entries'].append((int(index), int(endpoint), int(cluster, 16), int(mode), int(dest)))
    if not pages:
        raise ValueError('no binding pages')
    total = pages[0]['total']
    if total > 32:
        raise ValueError('total exceeds diagnostic capacity')
    next_index = 0
    entries = []
    for position, page in enumerate(pages):
        if position and next_index == total:
            raise ValueError('unexpected page after completion')
        if page['error'] or page['status'] or page['total'] != total or page['start'] != next_index:
            raise ValueError('invalid page status or sequence')
        if page['count'] != len(page['entries']) or page['count'] > total - next_index:
            raise ValueError('invalid page count')
        if not page['count'] and next_index != total:
            raise ValueError('empty intermediate page')
        for index, endpoint, cluster, mode, dest in page['entries']:
            if index != next_index or not 1 <= endpoint <= 254 or mode not in (1, 3):
                raise ValueError('invalid binding entry')
            entries.append((endpoint, cluster, mode, dest))
            next_index += 1
    if next_index != total:
        raise ValueError('incomplete binding table')
    pairs = collections.Counter((ep, cluster) for ep, cluster, _, _ in entries)
    return {'complete': True, 'pages': len(pages), 'entries': total,
            'duplicate_source_cluster_pairs': sum(n - 1 for n in pairs.values() if n > 1),
            'destination_modes': dict(collections.Counter(str(mode) for _, _, mode, _ in entries)),
            'limitation': 'Snapshot may change while pages are read; destination addresses were not captured.'}


if __name__ == '__main__':
    try:
        print(json.dumps(analyze(Path(sys.argv[1]).read_text(encoding='utf8', errors='replace')), indent=2))
    except (IndexError, OSError, ValueError) as error:
        print(f'Invalid binding evidence: {error}', file=sys.stderr)
        raise SystemExit(1)
