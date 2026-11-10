"""Condense a renderdoc-mcp debug_pixel trace (JSON) to one line per changed variable.

usage: rd_trace.py <trace.json> [max_lines]
"""
import json
import sys

d = json.load(open(sys.argv[1], encoding='utf-8'))
lim = int(sys.argv[2]) if len(sys.argv) > 2 else 10 ** 9
print('inputs:', [(i['name'], [round(v, 5) for v in i['floatValues']]) for i in d.get('inputs', [])])
n = 0
for s in d['trace']:
    for c in s['changes']:
        a = c['after']
        if a['type'] == 'Unknown':
            continue
        if a['members']:
            v = [(m['name'], (m['floatValues'] or m['uintValues'])[:4]) for m in a['members']][:4]
        else:
            v = a['floatValues'] or a['uintValues'] or a['intValues']
            v = [round(x, 5) if isinstance(x, float) else x for x in v][:8]
        print(s['step'], s['instruction'], a['name'], a['type'], v)
        n += 1
        if n >= lim:
            sys.exit(0)
