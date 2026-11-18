#!/usr/bin/env python3
"""Extract command names/numbers only, without reference code, comments or artwork."""
import hashlib
import json
import re
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit('Usage: audit_tc_commands.py TOTALCMD.INC output.json')
source = Path(sys.argv[1]).read_bytes()
commands = {}
for line in source.decode('latin1').splitlines():
    match = re.match(r'(cm_\w+)=(\d+);', line)
    if match:
        commands[match.group(1)] = int(match.group(2))
Path(sys.argv[2]).write_text(json.dumps({'sourceSha256': hashlib.sha256(source).hexdigest(),
                                       'commands': commands}, indent=2) + '\n')
