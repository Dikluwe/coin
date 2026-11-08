#!/usr/bin/env python3
"""Negative controls for the evidence gate; fixtures are copies, never source evidence."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from validation_common import INVENTORY

parser=argparse.ArgumentParser()
parser.add_argument('baseline',type=Path)
args=parser.parse_args()

class GateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.space=tempfile.TemporaryDirectory(prefix='coin-validation-tests-')
        cls.template=Path(cls.space.name)/'template'
        for backend, inventory in INVENTORY.items():
            dest=cls.template/backend;dest.mkdir(parents=True)
            for suffix in ('rgba','depth','semantic'):
                for name in inventory[suffix]: shutil.copy2(args.baseline/backend/name,dest/name)
            shutil.copy2(args.baseline/backend/'audit.log',dest/'audit.log')

            for phase in ('configure','build','audit','controls','run'): (dest/(phase+'.exit')).write_text('0\n')
            suite=ET.Element('testsuite')
            for name in inventory['controls']: ET.SubElement(suite,'testcase',name=name,status='run')
            ET.ElementTree(suite).write(dest/'controls.junit.xml')
    @classmethod
    def tearDownClass(cls): cls.space.cleanup()
    def setUp(self):
        self.root=Path(self.space.name)/self.id().rsplit('.',1)[-1]
        shutil.copytree(self.template,self.root)
    def gate(self, success, extra=()):
        result=subprocess.run([sys.executable,str(Path(__file__).with_name('compare.py')),str(self.root),
                               '--backends','recording','bgfx','wgpu',*extra],capture_output=True,text=True)
        self.assertEqual(result.returncode==0,success,result.stdout+result.stderr)
        self.assertEqual(json.loads((self.root/'comparison.json').read_text())['passed'],success)
    def test_complete_fixture_passes(self): self.gate(True)
    def test_missing_backend_fails(self): shutil.rmtree(self.root/'wgpu');self.gate(False)
    def test_missing_semantic_fails(self): next((self.root/'bgfx').glob('*.semantic')).unlink();self.gate(False)
    def test_truncated_image_fails(self): (self.root/'bgfx/indexed_published1.rgba').write_bytes(b'\0');self.gate(False)
    def test_wrong_pixels_fail(self): (self.root/'bgfx/indexed_published1.rgba').write_bytes(bytes(64*64*4));self.gate(False)
    def test_nonfinite_depth_fails(self):
        p=self.root/'bgfx/indexed_published1.depth';b=bytearray(p.read_bytes());b[:4]=struct.pack('<f',float('nan'));p.write_bytes(b);self.gate(False)
    def test_semantic_mismatch_fails(self):
        p=self.root/'bgfx/indexed_published_2_incremental.semantic';p.write_text(p.read_text().replace('V ', 'V 99 ',1));self.gate(False)
    def test_failed_audit_fails(self): (self.root/'bgfx/audit.exit').write_text('1\n');self.gate(False)
    def test_failed_controls_fails(self): (self.root/'wgpu/controls.exit').write_text('8\n');self.gate(False)
    def test_missing_control_fails(self):
        p=self.root/'bgfx/controls.junit.xml';tree=ET.parse(p);tree.getroot().remove(tree.getroot()[0]);tree.write(p);self.gate(False)
    def test_unexpected_stale_file_fails(self): (self.root/'bgfx/previous-run.rgba').write_bytes(bytes(64*64*4));self.gate(False)

    def test_negative_control_must_detect(self):
        for backend in INVENTORY:
            directory=self.root/backend
            shutil.copy2(directory/'indexed_published_2_baseline.semantic',directory/'indexed_published_2_incremental.semantic')
        self.gate(False)
    def test_pipeline3_complete_passes(self):
        shutil.copytree(self.root/'bgfx',self.root/'bgfx'/'pipeline3')
        self.gate(True,('--pipeline3',))
    def test_pipeline3_corruption_fails(self):
        shutil.copytree(self.root/'bgfx',self.root/'bgfx'/'pipeline3')
        (self.root/'bgfx/pipeline3/indexed_published1.rgba').write_bytes(bytes(64*64*4))
        self.gate(False,('--pipeline3',))
    def runner(self, audit=0, controls=0, pipeline=0, cache=0):
        # PATH stubs exercise shell aggregation, not GPU correctness. The
        # production runner has no bypass/test flag; all inputs are isolated.
        mock=self.root/'mock-bin';mock.mkdir()
        ram=self.root/'ram';out=self.root/'runner';fixture=self.template/'bgfx'
        audit_script=mock/'audit-template'
        audit_script.write_text("""#!/bin/bash
set -eu
out="$1"
cp "$MOCK_FIXTURE"/*.rgba "$MOCK_FIXTURE"/*.depth "$MOCK_FIXTURE"/*.semantic "$out/"
cat "$MOCK_FIXTURE/audit.log"
if [ "${COIN_BGFX_READBACK_PIPELINE_DEPTH:-1}" = 3 ]; then exit "$MOCK_PIPELINE_EXIT"; fi
exit "$MOCK_AUDIT_EXIT"
""")
        (mock/'cmake').write_text("""#!/bin/bash
set -eu
if [ "$1" != --build ]; then
  while [ "$1" != -B ]; do shift; done
  mkdir -p "$2/bin" "$2/lib"
  touch "$2/CMakeCache.txt" "$2/lib/libCoin.so.80.0.10"
else
  build="$2"; shift 2
  while [ "$1" != --target ]; do shift; done
  shift
  for name in "$@"; do cp "$MOCK_AUDIT_SCRIPT" "$build/bin/$name"; chmod +x "$build/bin/$name"; done
fi
""")
        (mock/'ctest').write_text("""#!/bin/bash
set -eu
while [ "$1" != --output-junit ]; do shift; done
cp "$MOCK_FIXTURE/controls.junit.xml" "$2"
exit "$MOCK_CONTROL_EXIT"
""")
        (mock/'cp').write_text("""#!/bin/bash
set -eu
if [ "$MOCK_CACHE_EXIT" != 0 ]; then
  for arg in "$@"; do if [ "$arg" = "$COIN_RETAINED_BUILD_ROOT/build/." ]; then exit "$MOCK_CACHE_EXIT"; fi; done
fi
exec /bin/cp "$@"
""")
        for name in ('cmake','ctest','cp'): (mock/name).chmod(0o755)
        env=dict(os.environ,PATH=str(mock)+os.pathsep+os.environ['PATH'],
                 COIN_RETAINED_BUILD_ROOT=str(ram),COIN_RETAINED_SEED_ROOT=str(self.root/'absent'),
                 MOCK_AUDIT_SCRIPT=str(audit_script),MOCK_FIXTURE=str(fixture),
                 MOCK_AUDIT_EXIT=str(audit),MOCK_CONTROL_EXIT=str(controls),MOCK_PIPELINE_EXIT=str(pipeline),MOCK_CACHE_EXIT=str(cache))
        result=subprocess.run(['bash',str(Path(__file__).with_name('build-run.sh')),'bgfx',str(self.root),str(out)],
                              env=env,capture_output=True,text=True)
        self.assertEqual(result.returncode==0,not(audit or controls or pipeline or cache),result.stdout+result.stderr)
        self.assertEqual((out/'bgfx/run.exit').read_text().strip(),'1' if audit or controls or pipeline or cache else '0')
    def test_runner_success(self): self.runner()
    def test_runner_propagates_cache_failure(self): self.runner(cache=9)
    def test_runner_propagates_audit_failure(self): self.runner(audit=7)
    def test_runner_propagates_ctest_failure(self): self.runner(controls=8)
    def test_runner_propagates_pipeline_failure(self): self.runner(pipeline=9)

unittest.main(argv=[sys.argv[0]],verbosity=2)
