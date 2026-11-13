"""Extract exact production functions; only USB transport/globals are mocked."""
import re
from pathlib import Path
root = Path(__file__).resolve().parent
source = (root / '../Core/Src/usbh_bluetooth.c').read_text()
functions = []
for name in ['BT_HCI_QueueCommand', 'BT_ProcessHCICommand', 'BT_QueuePairReply', 'BT_ProcessPairing']:
    match = re.search(r'^static [^;\n]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    start = source.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    functions.append(source[match.start():end])
(root / 'bin/hci_functions.inc').write_text('\n\n'.join(functions))
