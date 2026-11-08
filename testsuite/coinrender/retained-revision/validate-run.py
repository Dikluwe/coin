#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from validation_common import validate_run

parser = argparse.ArgumentParser(description='Reject failed or incomplete backend audit evidence.')
parser.add_argument('directory', type=Path)
parser.add_argument('--backend', choices=('recording', 'bgfx', 'wgpu'), required=True)
parser.add_argument('--audit-only', action='store_true', help='Validate an additional audit run without CTest controls.')
args = parser.parse_args()
errors = validate_run(args.directory, args.backend, not args.audit_only)
result = {'backend': args.backend, 'passed': not errors, 'errors': errors}
(args.directory / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
raise SystemExit(1 if errors else 0)
