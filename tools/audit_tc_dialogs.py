#!/usr/bin/env python3
"""Extract functional dialog metadata; omit artwork, code and event handlers.

Usage: audit_tc_dialogs.py TOTALCMD64.EXE output.json
Uses the same read-only FPC form parser as audit_tc_reference.py.
"""
import json
import re
import sys
from pathlib import Path
from audit_tc_reference import Reader, chunks

NAMES = ('ATTRIBDLG', 'DLGZIP', 'BUTTONCHANGEDLG', 'DLGUNZIP',
         'DLGCONFIGALL', 'FindFile', 'MultiRename', 'InpComboDlg')
PROPERTIES = set('AutoSize Cancel Caption Checked Default Enabled Height ItemIndex '
                 'Items.Strings Left ModalResult Position Sections State Style Tag '
                 'Text Top Visible Width WordWrap'.split())


def functional(node):
    return {'type': node['type'], 'name': node['name'],
            'properties': {key: value for key, value in node['properties'].items()
                           if key in PROPERTIES},
            'children': [functional(child) for child in node['children']
                         if child['type'] not in {'TPopupMenu', 'TMenuItem', 'TResolutionFix', 'TTimer', 'TTranslate'}]}


def extract(executable):
    binary = Path(executable).read_bytes()
    result = {}
    for name in NAMES:
        class_name = ('T' + name).encode('ascii')
        signature = rb'TPF0[\xf0-\xf7]?' + re.escape(bytes([len(class_name)]) + class_name)
        match = re.search(signature, binary)
        if match is None:
            raise ValueError(f'Reference form not found: {name}')
        result[name] = functional(Reader(chunks(binary, match.start()), 4).obj())
    return result


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    Path(sys.argv[2]).write_text(json.dumps(extract(sys.argv[1]), ensure_ascii=True,
                                          indent=2) + '\n')
