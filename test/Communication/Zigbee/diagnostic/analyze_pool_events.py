"""Analyze a bounded diagnostic snapshot; never infer allocations outside it."""
import json
import re
import sys
from collections import Counter
from pathlib import Path


def analyze(text):
    windows = []
    text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)
    for line in text.splitlines():
        header = re.search(r"RPTEVENT: window=(\d+):(\d+)", line)
        if header:
            first, end = map(int, header.groups())
            if end < first or end - first > 256:
                raise ValueError("invalid snapshot bounds")
            windows.append((first, end, {}))
        data = re.search(r"RPTEVENT: data=([^\s]*)", line)
        if data and windows:
            events = windows[-1][2]
            for item in filter(None, data.group(1).split(',')):
                match = re.fullmatch(r"(\d+):([MAXZFGBCD]):([0-9a-fA-F]+):([0-9a-fA-F]+)", item)
                if not match:
                    raise ValueError("invalid event")
                seq, kind, pointer, origin = match.groups()
                seq = int(seq)
                if seq in events:
                    raise ValueError("duplicate event")
                events[seq] = (kind, int(pointer, 16), int(origin, 16))
    if not windows:
        raise ValueError("no snapshot")
    results = []
    for first, end, events in windows:
        if set(events) != set(range(first, end)) or any(e[0] == 'G' for e in events.values()):
            raise ValueError("incomplete snapshot")
        active, buffers, counts = {}, set(), Counter()
        for seq in range(first, end):
            kind, pointer, origin = events[seq]
            counts[kind] += 1
            if kind == 'M':
                if not pointer or pointer in active:
                    raise ValueError("ambiguous allocation")
                active[pointer] = None
            elif kind == 'A':
                if pointer in active:
                    active[pointer] = origin
                else:
                    counts['unknown_origin_allocation'] += 1
            elif kind == 'F':
                if pointer in active:
                    del active[pointer]
                else:
                    counts['unmatched_free'] += 1
            elif kind == 'B':
                if not pointer or pointer in buffers:
                    raise ValueError("ambiguous buffer allocation")
                buffers.add(pointer)
            elif kind == 'D':
                if pointer in buffers:
                    buffers.remove(pointer)
                else:
                    counts['unmatched_buffer_free'] += 1
        results.append({'window': [first, end], 'counts': dict(counts),
                        'unfreed_in_window': dict(Counter(hex(x) if x is not None else 'unknown' for x in active.values())),
                        'unfreed_buffers_in_window': len(buffers) if any(k in counts for k in 'BCD') else None,
                        'limitation': 'Window-only observation; prior allocations and unwrapped calls are unknown.'})
    return results


if __name__ == '__main__':
    try:
        print(json.dumps(analyze(Path(sys.argv[1]).read_text(encoding='utf-8', errors='replace')), indent=2))
    except (ValueError, OSError, IndexError) as exc:
        print(f'Invalid evidence: {exc}', file=sys.stderr)
        raise SystemExit(1)
