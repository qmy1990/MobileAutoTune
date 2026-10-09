#!/usr/bin/env python3
"""Verify vendored dependency bytes against checked-in SHA256 inventory."""
import hashlib,json,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1]
lock=json.loads((root/'third_party/lock.json').read_text())
errors=[]
for relative,expected in lock['files'].items():
    path=root/relative
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest()!=expected:errors.append(relative)
if errors:sys.exit('Vendor hash mismatch: '+', '.join(errors))
print(f'Verified {len(lock["files"])} dependency files')
