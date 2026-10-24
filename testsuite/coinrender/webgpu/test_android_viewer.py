#!/usr/bin/env python3
"""Independent CLI/APK checks for the dedicated HTTPS WebView viewer.

python test_android_viewer.py --sdk SDK --ca PUBLIC_CA --work-dir /dev/shm/task -v
Optional --builder PATH, --url HTTPS_URL. Requires openssl and the supplied SDK.
No APK is installed; navigation behavior must additionally be exercised on a TV.
All generated keys, APKs and tool output remain in the supplied RAM directory.
"""
import argparse
import ast
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest
from urllib.parse import urlsplit
import zipfile

OPTIONS = None
PACKAGE = 'org.coin3d.coinrender.webgpuviewer'


def command(arguments, *, data=None, timeout=240):
    return subprocess.run([str(item) for item in arguments], input=data,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=timeout, env={**os.environ, 'TMPDIR':str(OPTIONS.work_dir)})


def checked(arguments, data=None):
    result = command(arguments, data=data)
    if result.returncode:
        raise AssertionError(result.stdout.decode(errors='replace') +
                             result.stderr.decode(errors='replace'))
    return result.stdout


def der(data):
    form = 'PEM' if b'-----BEGIN CERTIFICATE-----' in data else 'DER'
    return checked(['openssl','x509','-inform',form,'-outform','DER'], data)


def xml_nodes(text):
    """Parse aapt2's element/attribute tree, without assuming resource IDs."""
    nodes, stack = [], []
    for line in text.splitlines():
        match = re.match(r'(\s*)E: ([^\s(]+)', line)
        if match:
            indent, tag = len(match[1]), match[2]
            while stack and stack[-1]['indent'] >= indent:
                stack.pop()
            node = {'tag':tag,'attrs':{},'texts':[],
                    'parent':stack[-1] if stack else None,'indent':indent}
            nodes.append(node)
            stack.append(node)
        match = re.match(r'\s*A: ([^\s=(]+)(?:\([^)]*\))?=(.*)', line)
        if match and stack:
            raw = match[2].strip()
            quoted = re.match(r'"((?:[^"\\]|\\.)*)"', raw)
            value = json.loads('"' + quoted[1] + '"') if quoted else raw
            key=match[1]
            namespace='http://schemas.android.com/apk/res/android:'
            if key.startswith(namespace):
                key='android:'+key[len(namespace):]
            stack[-1]['attrs'][key] = value
        text_match=re.match(r'\s*[CT]:\s*(.*)',line)
        if text_match and stack:
            raw=text_match[1].strip()
            stack[-1]['texts'].append(ast.literal_eval(raw) if raw.startswith(('"',"'")) else raw)
    return nodes


def true(value):
    return value is not None and (value == 'true' or value.endswith('0xffffffff') or value.endswith('0x1'))


def false(value):
    return value is not None and (value == 'false' or value.endswith('0x0'))


def dex_metadata(data):
    assert data.startswith(b'dex\n'), 'Expected a DEX file'
    count, offset = struct.unpack_from('<II', data, 56)
    strings = []
    for index in range(count):
        at = struct.unpack_from('<I', data, offset + index * 4)[0]
        while data[at] & 128:
            at += 1
        at += 1
        strings.append(data[at:data.index(0,at)].decode('utf-8', errors='replace'))
    count, offset = struct.unpack_from('<II', data, 64)
    types = [strings[struct.unpack_from('<I',data,offset+i*4)[0]] for i in range(count)]
    count, offset = struct.unpack_from('<II', data, 88)
    methods = set()
    for index in range(count):
        owner, _, name = struct.unpack_from('<HHI',data,offset+index*8)
        methods.add((types[owner],strings[name]))
    return strings, methods


class AndroidViewer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        work = Path(OPTIONS.work_dir).resolve()
        if work != Path('/dev/shm') and Path('/dev/shm') not in work.parents:
            raise AssertionError('--work-dir must be inside /dev/shm')
        work.mkdir(parents=True, exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix='independent-viewer-tests-', dir=work)
        cls.root = Path(cls.temporary.name)
        tools = sorted((Path(OPTIONS.sdk)/'build-tools').iterdir(),
                       key=lambda p:tuple(int(x) for x in re.findall(r'\d+',p.name)))
        cls.tools = tools[-1]
        cls.aapt2 = cls.tools/'aapt2'
        cls.apk = cls.root/'viewer.apk'
        cls.build(cls.root/'valid-build',cls.apk,OPTIONS.ca,OPTIONS.url, expected_success=True)
        cls.archive = zipfile.ZipFile(cls.apk)
        cls.manifest_text = cls.xml('AndroidManifest.xml')
        cls.manifest = xml_nodes(cls.manifest_text)
        cls.strings, cls.methods = set(), set()
        for name in cls.archive.namelist():
            if re.fullmatch(r'classes\d*\.dex',name):
                strings, methods = dex_metadata(cls.archive.read(name))
                cls.strings.update(strings)
                cls.methods.update(methods)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'archive'):
            cls.archive.close()
        cls.temporary.cleanup()

    @classmethod
    def build(cls, directory, apk, ca, url, expected_success):
        result = command(['python3',OPTIONS.builder,'--sdk',OPTIONS.sdk,
                          '--build-dir',directory,'--output-apk',apk,'--ca',ca,'--url',url])
        output = (result.stdout+result.stderr).decode(errors='replace')
        if expected_success:
            if result.returncode != 0 or not Path(apk).is_file():
                raise AssertionError(f'Valid build failed ({result.returncode}):\n{output}')
        elif result.returncode == 0 or Path(apk).exists():
            raise AssertionError(f'Invalid input created an APK ({result.returncode}):\n{output}')
        return output

    @classmethod
    def xml(cls, name):
        return checked([cls.aapt2,'dump','xmltree','--file',name,cls.apk]).decode()

    def test_manifest_is_minimal_tv_application(self):
        def nodes(tag):
            return [n for n in self.manifest if n['tag']==tag]
        self.assertEqual(nodes('manifest')[0]['attrs']['package'], PACKAGE)
        permissions = [n['attrs'].get('android:name') for n in self.manifest
                       if n['tag'].startswith('uses-permission')]
        self.assertEqual(permissions,['android.permission.INTERNET'])
        sdk = nodes('uses-sdk')[0]['attrs']
        def number(value):
            return int(re.search(r'(0x[0-9a-f]+|\d+)$',value)[1],0)
        self.assertEqual(number(sdk['android:minSdkVersion']),23)
        self.assertGreaterEqual(number(sdk['android:targetSdkVersion']),30)
        categories = [n['attrs'].get('android:name') for n in nodes('category')]
        self.assertIn('android.intent.category.LEANBACK_LAUNCHER',categories)
        self.assertTrue(any(n['attrs'].get('android:name')=='android.intent.action.MAIN'
                            for n in nodes('action')))
        app = nodes('application')[0]['attrs']
        self.assertTrue(false(app.get('android:usesCleartextTraffic')),app)
        self.assertIn('android:networkSecurityConfig',app)
        launcher=next(n for n in nodes('category')
                      if n['attrs'].get('android:name')=='android.intent.category.LEANBACK_LAUNCHER')
        intent=launcher['parent']
        self.assertEqual(intent['tag'],'intent-filter')
        self.assertTrue(any(n['parent'] is intent and n['attrs'].get('android:name')==
                            'android.intent.action.MAIN' for n in nodes('action')))
        activity=intent['parent']
        self.assertEqual(activity['tag'],'activity')
        self.assertIn(activity['attrs'].get('android:screenOrientation'),
                      ('landscape','0','0x0','(type 0x10)0x0'))
        # With minSDK23/target30 the platform default is hardware acceleration enabled.
        acceleration=activity['attrs'].get('android:hardwareAccelerated',
                                           app.get('android:hardwareAccelerated'))
        self.assertTrue(acceleration is None or true(acceleration))

    def test_signed_apk_contains_only_the_public_ca(self):
        checked([self.tools/'apksigner','verify','--verbose',self.apk])
        expected = der(Path(OPTIONS.ca).read_bytes())
        public_cas = []
        for name in self.archive.namelist():
            data = self.archive.read(name)
            self.assertNotIn(b'PRIVATE KEY-----',data,name)
            self.assertNotRegex(name.lower(),r'\.(?:key|p12|pfx|jks|keystore)$')
            if name.startswith('res/raw/') and not name.endswith('/'):
                self.assertEqual(der(data),expected,name)
                public_cas.append(name)
        self.assertEqual(len(public_cas),1,public_cas)

    def test_network_security_config_scopes_ca_to_exact_host(self):
        candidates = []
        for name in self.archive.namelist():
            if name.startswith('res/xml/') and name.endswith('.xml'):
                text = self.xml(name)
                if 'E: network-security-config' in text:
                    candidates.append((name,xml_nodes(text),text))
        self.assertEqual(len(candidates),1,candidates)
        name, nodes, text = candidates[0]
        domains = [n for n in nodes if n['tag']=='domain']
        self.assertEqual(len(domains),1,text)
        domain = domains[0]
        self.assertFalse(true(domain['attrs'].get('includeSubdomains')),text)
        # SDK versions print XML text nodes as C: "..." or T: '...'.
        host = urlsplit(OPTIONS.url).hostname
        self.assertEqual(''.join(domain['texts']).strip(),host,text)
        anchors = [n for n in nodes if n['tag']=='certificates']
        self.assertEqual(len(anchors),1,text)
        self.assertTrue(anchors[0]['attrs'].get('src','').startswith('@'),text)
        ancestor = anchors[0]['parent']
        ancestry = []
        while ancestor:
            ancestry.append(ancestor['tag']); ancestor=ancestor['parent']
        self.assertIn('domain-config',ancestry,text)
        self.assertNotIn('base-config',ancestry,text)
        self.assertNotIn('debug-overrides',ancestry,text)
        self.assertNotRegex(text,r'cleartextTrafficPermitted[^\n]*(?:true|0xffffffff)')
        # Prove the manifest references this config and its anchor references the CA resource.
        resources = checked([self.aapt2,'dump','resources',self.apk]).decode()
        app = next(n for n in self.manifest if n['tag']=='application')['attrs']
        config_id = re.search(r'0x[0-9a-fA-F]+',app['android:networkSecurityConfig'])[0]
        anchor_id = re.search(r'0x[0-9a-fA-F]+',anchors[0]['attrs']['src'])[0]
        for resource_id, resource_path in ((config_id,name),
                                           (anchor_id,next(n for n in self.archive.namelist()
                                                           if n.startswith('res/raw/')))):
            block = re.search(r'resource\s+'+re.escape(resource_id)+r'\b.*?(?=\n\s*resource\s+0x|\Z)',
                              resources,re.S)
            self.assertIsNotNone(block,resources)
            self.assertIn(resource_path,block[0])

    def test_dex_has_webview_diagnostics_and_no_tls_bypass_or_bridge(self):
        for method in ('setJavaScriptEnabled','setDomStorageEnabled','loadUrl',
                       'evaluateJavascript','shouldOverrideUrlLoading','shouldInterceptRequest'):
            self.assertTrue(any(name==method for _,name in self.methods),method)
        joined='\n'.join(self.strings)
        for value in ('isSecureContext','navigator.gpu','coinRenderWebGPU',OPTIONS.url):
            self.assertIn(value,joined)
        for owner, method in self.methods:
            self.assertNotEqual((owner,method),('Landroid/webkit/SslErrorHandler;','proceed'))
            self.assertNotEqual((owner,method),('Landroid/webkit/WebView;','addJavascriptInterface'))
            self.assertNotIn(method,('setDefaultSSLSocketFactory','setDefaultHostnameVerifier',
                                     'installCaCert','setDeviceOwner','setProfileOwner'))
            if owner.startswith('Landroid/provider/Settings$'):
                self.assertFalse(method.startswith('put'),(owner,method))
        self.assertTrue(any(name in ('onReceivedError','onReceivedHttpError')
                            for _,name in self.methods),'WebView errors must be reported')

    def test_cli_handles_paths_with_shell_metacharacters_as_data(self):
        # Unquoted shell interpolation either breaks this build or creates a command.
        directory=self.root/"build 'quoted' ; literal"
        ca=self.root/"public CA 'quoted' ; literal.crt"
        apk=self.root/"viewer 'quoted' ; literal.apk"
        ca.write_bytes(Path(OPTIONS.ca).read_bytes())
        self.build(directory,apk,ca,OPTIONS.url,True)
        checked([self.tools/'apksigner','verify',apk])

    def test_rejects_non_https_and_malformed_origins(self):
        for index,url in enumerate(('http://192.168.0.16:8443/','file:///etc/passwd',
                                    'javascript:alert(1)','data:text/html,hello',
                                    'https:///missing-host','https://example.test:bad/')):
            with self.subTest(url=url):
                self.build(self.root/f'bad-url-{index}',self.root/f'bad-url-{index}.apk',
                           OPTIONS.ca,url,expected_success=False)

    def test_rejects_invalid_non_ca_and_expired_certificates(self):
        bad=self.root/'invalid.crt'
        bad.write_text('not an X509 certificate')
        self.build(self.root/'bad-cert',self.root/'bad-cert.apk',bad,OPTIONS.url,False)
        for label,constraints,days in (('leaf','critical,CA:FALSE','1'),
                                        ('expired','critical,CA:TRUE','-1')):
            with self.subTest(certificate=label):
                key=self.root/(label+'.key')
                cert=self.root/(label+'.crt')
                checked(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:P-256',
                         '-nodes','-keyout',key,'-out',cert,'-days','1','-subj','/CN=Independent test',
                         '-addext','basicConstraints='+constraints])
                if days=='-1':
                    expired=self.root/'expired-final.crt'
                    checked(['openssl','x509','-in',cert,'-signkey',key,'-days','-1','-out',expired])
                    cert=expired
                self.build(self.root/('bad-'+label),self.root/('bad-'+label+'.apk'),
                           cert,OPTIONS.url,False)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',required=True)
    parser.add_argument('--ca',required=True)
    parser.add_argument('--work-dir',required=True)
    parser.add_argument('--builder',default=str(Path(__file__).resolve().parents[3]/
        'examples/coinrender/webgpu/android_viewer/build.py'))
    parser.add_argument('--url',default='https://192.168.0.16:8443/')
    OPTIONS,extra=parser.parse_known_args()
    unittest.main(argv=[__file__]+extra)
