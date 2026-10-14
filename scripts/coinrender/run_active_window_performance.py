#!/usr/bin/env python3
"""Run window A/B with display-power checks outside measured processes.

Pass the normal performance runner arguments after this helper's options.
--manage-dpms temporarily powers the display on and disables DPMS, then restores
the original state. It does not modify screen-lock settings. Without this flag
the helper only checks the existing display state and rejects powered-off runs.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

import run_performance_continuation as performance


def require_active(text):
    if 'Monitor is Off' in text or 'Monitor is Standby' in text or 'Monitor is Suspend' in text:
        raise RuntimeError('Display power is inactive; this window timing is not qualified')
    if 'DPMS is Disabled' not in text and 'Monitor is On' not in text:
        raise RuntimeError('Display power status is unavailable; use a controlled session')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit-dir', required=True, type=Path)
    parser.add_argument('--manage-dpms', action='store_true')
    options, forwarded = parser.parse_known_args()
    scope_parser = argparse.ArgumentParser(add_help=False)
    scope_parser.add_argument('--scope', default='window')
    scope_parser.add_argument('--mode', default='measure')
    scope_options, _ = scope_parser.parse_known_args(forwarded)
    if scope_options.scope != 'window' or scope_options.mode != 'measure':
        parser.error('This helper handles window measurements; PPM verification uses the offscreen runner')
    options.audit_dir.mkdir(parents=True, exist_ok=True)
    if '--scope' not in forwarded and not any(v.startswith('--scope=') for v in forwarded):
        forwarded += ['--scope', 'window']
    original_run = subprocess.run
    audit = []

    def xset(*arguments):
        process = original_run(['xset', *arguments], env=os.environ.copy(), text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
        return process.stdout

    def check(label, command=None):
        state = xset('q')
        audit.append({'label': label, 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                      'command': command, 'state': state})
        (options.audit_dir / 'audit.json').write_text(json.dumps(audit, indent=2) + '\n')
        require_active(state)

    def monitored_run(command, *arguments, **keywords):
        measured = isinstance(command, list) and command and command[0] == '/usr/bin/time'
        if measured:
            check('before', command)
        process = original_run(command, *arguments, **keywords)
        if measured:
            try:
                check('after', command)
            except RuntimeError:
                (options.audit_dir / 'excluded-process.log').write_text(
                    str(process.stdout) + str(process.stderr))
                raise
        return process

    before = xset('q')
    (options.audit_dir / 'original.log').write_text(before)
    try:
        if options.manage_dpms:
            xset('+dpms')
            xset('dpms', 'force', 'on')
            check('forced-on')
            xset('-dpms')
        check('preflight')
        subprocess.run = monitored_run
        sys.argv = [sys.argv[0], *forwarded]
        performance.main()
    finally:
        subprocess.run = original_run
        if options.manage_dpms:
            xset('+dpms' if 'DPMS is Enabled' in before else '-dpms')
            if 'Monitor is Off' in before:
                xset('dpms', 'force', 'off')
            elif 'Monitor is Standby' in before:
                xset('dpms', 'force', 'standby')
            elif 'Monitor is Suspend' in before:
                xset('dpms', 'force', 'suspend')
            (options.audit_dir / 'restored.log').write_text(xset('q'))


if __name__ == '__main__':
    main()
