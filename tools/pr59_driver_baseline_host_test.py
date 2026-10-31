#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：固定用户选定PR59底层字节，允许界面独立演进。/ English: Pin user-selected PR59 driver bytes while UI evolves independently."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
marker=ROOT/'.kiiko-source-view.json'
SOURCE=Path(json.loads(marker.read_text())['source']) if marker.exists() else ROOT
j=json.loads((SOURCE/'tools/fixtures/pr59-driver-baseline.json').read_text())
for name,digest in j['sha256'].items():assert hashlib.sha256((SOURCE/name).read_bytes()).hexdigest()==digest,name
print('PASS:',len(j['sha256']),'driver/config/waveform files match selected PR59',j['source_commit'])
