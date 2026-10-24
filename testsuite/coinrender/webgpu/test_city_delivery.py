#!/usr/bin/env python3
"""Generator compatibility and artifact-only HTTP loopback server checks.

--baseline-generator must point to an untouched historical generator copied to
RAM by C (git show BASE:examples/coinrender/generate_large_scene.py > RAM/file).
--artifacts-dir is an actual completed build, never the repository tree.
All fixtures/process logs/output go into a unique child of --ram-dir /dev/shm/...
"""
import argparse
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen,Request

OPTIONS=None


def run(arguments,timeout=120):
    return subprocess.run([str(x) for x in arguments],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                          timeout=timeout,env={**os.environ,'TMPDIR':OPTIONS.ram_dir})


class CityDelivery(unittest.TestCase):
    def setUp(self):
        ram=Path(OPTIONS.ram_dir).resolve()
        self.assertTrue(ram==Path('/dev/shm') or Path('/dev/shm') in ram.parents)
        ram.mkdir(parents=True,exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='city-delivery-',dir=ram)
        self.root=Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def test_historical_inventor_output_is_byte_identical(self):
        if not OPTIONS.baseline_generator:
            self.skipTest('--baseline-generator required for historical byte comparison')
        grids=(1,10,11,200)+( (1000,) if OPTIONS.million_generator else () )
        for grid in grids:
            for seed in (136,23):
                with self.subTest(grid=grid,seed=seed):
                    expected=self.root/'baseline.iv';actual=self.root/'candidate.iv'
                    for script,output in ((OPTIONS.baseline_generator,expected),
                                          (OPTIONS.generator,actual)):
                        result=run(['python3',script,output,'--grid',grid,'--seed',seed],timeout=240)
                        self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
                    self.assertEqual(expected.stat().st_size,actual.stat().st_size)
                    with expected.open('rb') as left,actual.open('rb') as right:
                        offset=0
                        while True:
                            block=left.read(1024*1024)
                            self.assertEqual(block,right.read(1024*1024),f'Changed .iv bytes near {offset}')
                            if not block:break
                            offset+=len(block)
                    expected.unlink();actual.unlink()

    def artifacts(self):
        if not OPTIONS.artifacts_dir:self.skipTest('--artifacts-dir required for HTTP tests')
        root=self.root/'public'
        shutil.copytree(OPTIONS.artifacts_dir,root,symlinks=True)
        return root

    def command(self,root,port,bind='127.0.0.1',reports=False):
        return ['python3',OPTIONS.server,'--directory',root,'--port',str(port),
                '--bind',bind,'--loopback-http']+(['--benchmark-reports'] if reports else [])

    def start(self,root,reports=False):
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
        log=(self.root/'server.log').open('wb');self.addCleanup(log.close)
        process=subprocess.Popen([str(x) for x in self.command(root,port,reports=reports)],stdout=log,stderr=log,
                                 env={**os.environ,'TMPDIR':OPTIONS.ram_dir})
        def close():
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=3)
                except subprocess.TimeoutExpired:process.kill();process.wait()
        self.addCleanup(close)
        origin=f'http://127.0.0.1:{port}'
        for _ in range(100):
            if process.poll() is not None:
                self.fail('Loopback HTTP server exited: '+(self.root/'server.log').read_text())
            try:
                with urlopen(origin+'/',timeout=0.2) as response:
                    self.assertEqual(response.status,200)
                    break
            except OSError:time.sleep(0.05)
        else:self.fail('Loopback HTTP server did not become ready')
        return origin,port

    def status(self,origin,path,method='GET'):
        try:
            with urlopen(Request(origin+path,method=method),timeout=3) as response:
                return response.status,response.read()
        except HTTPError as error:return error.code,error.read()

    def test_loopback_http_is_private_and_artifact_allowlisted(self):
        root=self.artifacts()
        for name in ('private.key','execution.log','CMakeLists.txt','not-a-city.bin','manifest.json'):
            (root/name).write_text('PRIVATE TEST SENTINEL')
        (root/'nested').mkdir(exist_ok=True)
        (root/'nested'/'index.html').write_text('PRIVATE TEST SENTINEL')
        origin,port=self.start(root)
        # Inspect the actual listen address, rather than trusting CLI output.
        listeners=[]
        for table in ('/proc/net/tcp','/proc/net/tcp6'):
            for line in Path(table).read_text().splitlines()[1:]:
                columns=line.split();address,hex_port=columns[1].split(':')
                if int(hex_port,16)==port and columns[3]=='0A':listeners.append(address)
        self.assertTrue(listeners,'Server TCP listener not found')
        self.assertTrue(all(address=='0100007F' for address in listeners),listeners)
        paths=('/private.key','/execution.log','/CMakeLists.txt','/not-a-city.bin','/manifest.json',
               '/nested/','/nested/index.html','/.git/config','/../private.key',
               '/%2e%2e/private.key','/%2e%2e%2fprivate.key','/%252e%252e/private.key')
        for path in paths:
            for method in ('GET','HEAD'):
                with self.subTest(path=path,method=method):
                    status,data=self.status(origin,path,method)
                    self.assertIn(status,(400,403,404))
                    self.assertNotIn(b'PRIVATE TEST SENTINEL',data)
        self.assertEqual(self.status(origin,'/index.html')[0],200)
        self.assertEqual(self.status(origin,'/app.js')[0],200)

    def test_http_rejects_public_or_nonloopback_bind(self):
        root=self.artifacts()
        for bind in ('0.0.0.0','::','192.0.2.1'):
            with self.subTest(bind=bind):
                try:result=run(self.command(root,0,bind),timeout=4)
                except subprocess.TimeoutExpired:self.fail(f'HTTP accepted nonloopback bind {bind}')
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)

    def post(self,port,body=b'{}',path='/benchmark-report',content_type='application/json',
             origin=None,length='auto'):
        connection=http.client.HTTPConnection('127.0.0.1',port,timeout=3)
        try:
            connection.putrequest('POST',path)
            if content_type is not None:connection.putheader('Content-Type',content_type)
            if origin is not None:connection.putheader('Origin',origin)
            if length=='auto':connection.putheader('Content-Length',str(len(body)))
            elif length is not None:connection.putheader('Content-Length',str(length))
            connection.endheaders(body)
            response=connection.getresponse()
            return response.status,response.read()
        finally:connection.close()

    def test_report_endpoint_disabled_without_explicit_server_optin(self):
        _,port=self.start(self.artifacts())
        status,_=self.post(port,origin=f'http://127.0.0.1:{port}')
        self.assertIn(status,(403,404,405))

    def test_report_endpoint_accepts_only_bounded_same_origin_json_objects(self):
        origin,port=self.start(self.artifacts(),reports=True)
        target=self.root/'must-not-be-written'
        report={'status':'rendering','path':str(target),'scene':{'name':'city-40000'},
                'marker':'ACCEPTED_REPORT_MARKER'}
        body=json.dumps(report).encode()
        status,ack=self.post(port,body,origin=origin)
        self.assertIn(status,(200,201,202))
        self.assertLessEqual(len(ack),1024)
        self.assertIsInstance(json.loads(ack),dict)
        self.assertFalse(target.exists(),'Client JSON path was treated as filesystem output')
        for _ in range(40):
            log=(self.root/'server.log').read_text()
            if 'BENCHMARK_REPORT ' in log:break
            time.sleep(0.05)
        lines=[line.split('BENCHMARK_REPORT ',1)[1] for line in log.splitlines()
               if line.startswith('BENCHMARK_REPORT ')]
        self.assertEqual(len(lines),1,log)
        self.assertEqual(json.loads(lines[0]),report)
        cases=(
            {'body':b'[]'}, {'body':b'null'}, {'body':b'{bad'},
            {'body':b'{"marker":"REJECTED_REPORT_MARKER"','content_type':'application/json'},
            {'content_type':'text/plain'}, {'content_type':None},
            {'body':b'{}','length':None}, {'length':-1}, {'length':'not-a-number'},
            {'body':b' '*65537}, {'origin':'http://foreign.invalid'},
            {'origin':origin.replace('http:','https:')},
            {'origin':f'http://127.0.0.1:{port+1}'},
            {'path':'/private-report'}, {'path':'/benchmark-report/../private.key'},
        )
        for case in cases:
            with self.subTest(case=case):
                arguments={'origin':origin,**case}
                status,_=self.post(port,**arguments)
                self.assertIn(status,(400,403,404,405,411,413,415))
        log=(self.root/'server.log').read_text()
        self.assertNotIn('REJECTED_REPORT_MARKER',log)
        self.assertEqual(sum(line.startswith('BENCHMARK_REPORT ') for line in log.splitlines()),1)
        self.assertFalse(target.exists())

    def test_report_optin_cannot_enable_nonloopback_http_or_https(self):
        root=self.artifacts()
        for bind in ('0.0.0.0','::','192.0.2.1'):
            with self.subTest(bind=bind):
                try:result=run(self.command(root,0,bind,reports=True),timeout=4)
                except subprocess.TimeoutExpired:self.fail('Reports enabled a public HTTP listener')
                self.assertNotEqual(result.returncode,0)
        # Without --loopback-http, telemetry must be refused even with an otherwise valid TLS identity.
        key=self.root/'test-tls.key';cert=self.root/'test-tls.crt'
        result=run(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:P-256',
                    '-nodes','-keyout',key,'-out',cert,'-days','1','-subj','/CN=localhost',
                    '-addext','subjectAltName=DNS:localhost,IP:127.0.0.1'])
        self.assertEqual(result.returncode,0,result.stderr)
        arguments=['python3',OPTIONS.server,'--directory',root,'--port','0','--bind','127.0.0.1',
                   '--cert',cert,'--key',key,'--benchmark-reports']
        try:result=run(arguments,timeout=4)
        except subprocess.TimeoutExpired:self.fail('Reports were enabled outside explicit loopback HTTP mode')
        self.assertNotEqual(result.returncode,0)

    def test_symlink_for_allowed_artifact_never_exposes_target(self):
        root=self.artifacts();secret=self.root/'outside-secret'
        secret.write_text('PRIVATE TEST SENTINEL')
        (root/'app.js').unlink();(root/'app.js').symlink_to(secret)
        result=run(self.command(root,0),timeout=4)
        self.assertNotEqual(result.returncode,0,'Server accepted symlink for required JS artifact')


if __name__=='__main__':
    repository=Path(__file__).resolve().parents[3]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ram-dir',required=True)
    parser.add_argument('--baseline-generator')
    parser.add_argument('--generator',default=str(repository/'examples/coinrender/generate_large_scene.py'))
    parser.add_argument('--server',default=str(repository/'examples/coinrender/webgpu/serve.py'))
    parser.add_argument('--artifacts-dir')
    parser.add_argument('--million-generator',action='store_true')
    OPTIONS,extra=parser.parse_known_args()
    unittest.main(argv=[__file__]+extra)
