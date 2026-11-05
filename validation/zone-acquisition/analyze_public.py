import collections
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description='Analyser la capture publiée dans le ticket nº 7, sans contacter un appareil.')
parser.add_argument('issue_json', type=Path, help='Fichier JSON contenant le corps du ticket public')
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
body = json.loads(args.issue_json.read_text())['body']
frames = []
for line in body.splitlines():
    cells = [c.strip() for c in line.split('|')]
    if len(cells) != 3 or not cells[0].startswith('2025-'):
        continue
    words = cells[2].split()
    if len(words) != 4:
        raise ValueError(cells[2])
    packet = bytes.fromhex(words[2])
    assert len(packet) == int(words[1], 16)
    frames.append({'time': cells[0], 'label': cells[1], 'packet': packet})

requests = {}
pairs = []
for entry in frames:
    p = entry['packet']
    if len(p) == 23 and p[1] == 8 and p[5] == 0x17 and p[10:15] == bytes.fromhex('a02f000408'):
        requests[(p[2], p[3])] = entry
    elif len(p) == 49 and p[0] == 8 and p[1] == 0x80 and p[4:7] == bytes.fromhex('81172a'):
        request = requests.get((p[2], p[3]))
        if request:
            pairs.append({'request': request, 'reply': entry, 'satellite_mode': request['packet'][20], 'boiler_mode': p[24]})

print('trames', len(frames), 'paires associées', len(pairs))
print('paires de modes', {f'{a:02x}/{b:02x}': count for (a,b),count in collections.Counter((p['satellite_mode'], p['boiler_mode']) for p in pairs).items()})
print('réponses sans requête associée', sum(len(x['packet']) == 49 and x['packet'][:2] == bytes.fromhex('0880') for x in frames) - len(pairs))
for pair in pairs:
    print(pair['request']['time'], f"{pair['satellite_mode']:02x}", '->', pair['reply']['time'], f"{pair['boiler_mode']:02x}")

unique = {}
for p in pairs:
    raw = p['satellite_mode']
    if raw not in unique:
        unique[raw] = {'raw_mode': raw, 'request_hex': p['request']['packet'].hex(), 'reply_hex': p['reply']['packet'].hex()}
args.out.write_text(json.dumps(list(unique.values()), indent=2) + '\n')
