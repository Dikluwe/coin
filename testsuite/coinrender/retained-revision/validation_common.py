"""Strict inventory and execution receipts for the fixed retained audit fixtures."""
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

INVENTORY = json.loads(Path(__file__).with_name('expected-files.json').read_text())


def validate_run(directory, backend, controls=True):
    directory = Path(directory)
    errors = []
    expected = INVENTORY[backend]
    for suffix in ('rgba', 'depth', 'semantic'):
        actual = {p.name for p in directory.glob('*.' + suffix)}
        wanted = set(expected[suffix])
        for name in sorted(wanted - actual):
            errors.append('missing ' + str(directory / name))
        for name in sorted(actual - wanted):
            errors.append('unexpected ' + str(directory / name))
        for name in sorted(wanted & actual):
            path = directory / name
            if suffix in ('rgba', 'depth'):
                if path.stat().st_size != 64 * 64 * 4:
                    errors.append('invalid byte count: ' + str(path))
            else:
                try:
                    lines = path.read_text().splitlines()
                    if not lines:
                        raise ValueError('empty semantic receipt')
                    for line in lines:
                        values = line.split()
                        if not values or values[0] not in ('V', 'I', 'M', 'S', 'C', 'D'):
                            raise ValueError('invalid semantic record')
                        if not all(math.isfinite(float(value)) for value in values[1:]):
                            raise ValueError('non-finite semantic value')
                except (OSError, ValueError, UnicodeError) as exc:
                    errors.append(str(path) + ': ' + str(exc))
    phases = ['configure', 'build', 'audit', 'controls', 'run'] if controls else ['audit']
    for phase in phases:
        path = directory / (phase + '.exit')
        try:
            if path.read_text().strip() != '0':
                errors.append('failed phase: ' + str(path))
        except OSError:
            errors.append('missing phase receipt: ' + str(path))
    try:
        log = (directory / 'audit.log').read_text()
        summaries = re.findall(r'^SUMMARY observations=(\d+) failures=(\d+)$', log, re.M)
        if summaries != [(str(expected['observations']), '0')]:
            errors.append('incomplete or failed audit summary: ' + str(directory))
        if 'ASSERT_FAIL' in log:
            errors.append('audit assertion failure: ' + str(directory))
    except OSError:
        errors.append('missing audit log: ' + str(directory))
    if controls:
        try:
            tests = ET.parse(directory / 'controls.junit.xml').getroot().findall('.//testcase')
            names = [test.get('name') for test in tests]
            if sorted(names) != sorted(expected['controls']):
                errors.append('incomplete or unexpected CTest inventory: ' + str(directory))
            for test in tests:
                if test.find('failure') is not None or test.find('error') is not None or test.find('skipped') is not None or test.get('status') not in (None, 'run'):
                    errors.append('failed/skipped CTest: ' + str(test.get('name')))
        except (OSError, ET.ParseError) as exc:
            errors.append('invalid CTest receipt: ' + str(directory) + ': ' + str(exc))
    return errors
