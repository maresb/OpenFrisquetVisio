"""Compiler les méthodes du micrologiciel avec des substituts radio/MQTT et rejouer les captures."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def extract(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def stripped(path):
    return re.sub(r'^#.*\n', '', path.read_text(), flags=re.M)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fixtures', type=Path)
parser.add_argument('--baseline', action='store_true', help='Rejouer avec la révision amont 9214063, avant correction')
args = parser.parse_args()
def satellite_file(name):
    if args.baseline:
        return subprocess.check_output(['git', 'show', '9214063:src/Frisquet/' + name], cwd=ROOT, text=True)
    return (ROOT / 'src/Frisquet' / name).read_text()
sat = satellite_file('Satellite.cpp')
zone = (ROOT / 'src/Frisquet/Zone.cpp').read_text()
header = (ROOT / 'src/Frisquet/FrisquetRadio.h').read_text()
with tempfile.TemporaryDirectory(prefix='ofv-zone-replay-') as tmp:
    tmp = Path(tmp)
    # Réutiliser le format des trames et le lecteur du micrologiciel ; seule receiveExpected est simulée.
    wire = 'class FrisquetRadio { public:\n'
    for signature in ['enum MessageType', 'struct RadioTrameHeader', 'struct RadioTrameInit']:
        wire += extract(header, signature) + ';\n'
    wire += (HERE / 'radio-stub.inc').read_text() + '\n};\n'
    wire += stripped(ROOT / 'src/Buffer.h')
    (tmp / 'wire-types.inc').write_text(wire)
    (tmp / 'classes.inc').write_text(stripped(ROOT / 'src/Frisquet/Zone.h').replace('private:', 'public:')
        + re.sub(r'^#.*\n', '', satellite_file('Satellite.h'), flags=re.M).replace('private:', 'public:'))
    methods = zone[zone.index('namespace {'):zone.index('void Zone::loadConfig')]
    for signature in ['Zone::MODE_ZONE Zone::getMode(', 'void Zone::setMode(MODE_ZONE',
                      'byte Zone::getModeOptions(', 'void Zone::setModeOptions(',
                      'void Zone::desactiverBoost(', 'bool Zone::confortActif(',
                      'bool Zone::derogationActive(', 'void Zone::confirmerMode(MODE_ZONE',
                      'void Zone::publierMode(']:
        methods += extract(zone, signature) + '\n'
    for signature in ['bool Satellite::onReceive(', 'Satellite::MODE Satellite::getMode(',
                      'void Satellite::setMode(', 'Satellite::MODE Satellite::decodeModeFromRaw(',
                      'uint8_t Satellite::encodeModeForSend(',
                      ('void' if args.baseline else 'bool') + ' Satellite::setModeFromRaw(']:
        methods += extract(sat, signature) + '\n'
    (tmp / 'methods.inc').write_text(methods)
    fixtures = json.loads(args.fixtures.read_text())
    array = 'struct Fixture {unsigned raw; const char* request; const char* reply;};\nconst Fixture fixtures[] = {\n'
    for f in fixtures:
        array += '{%d, "%s", "%s"},\n' % (f['raw_mode'], f['request_hex'], f['reply_hex'])
    (tmp / 'fixtures.inc').write_text(array + '};\n')
    binary = tmp / 'replay'
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Wno-reorder', '-Wno-unused-variable', '-Wno-switch',
        '-DBASELINE=' + str(int(args.baseline)), '-g', '-fsanitize=address,undefined',
        '-fno-omit-frame-pointer', '-I', str(tmp), str(HERE/'replay.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
