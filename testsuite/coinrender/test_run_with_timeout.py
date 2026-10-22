#!/usr/bin/env python3
"""Black-box contract for the bounded subprocess watchdog; no renderer or GPU."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time
import unittest

RUNNER = None
SCRATCH = None
# Every fixture records its identity, allowing test cleanup even after a broken
# watchdog, without signalling a reused PID or an unrelated process group.
RECORD = r'''
import json, os, signal, subprocess, sys, time
from pathlib import Path
root = Path(os.environ["WATCHDOG_TEST_ROOT"])
def record(name):
    pid = os.getpid()
    raw = Path("/proc/%d/stat" % pid).read_text()
    start = raw[raw.rfind(")") + 2:].split()[19]
    (root / (name + ".pid.json")).write_text(json.dumps([pid, start]))
    return pid
'''


def identity_alive(identity):
    pid, start = identity
    try:
        raw = Path("/proc/%d/stat" % pid).read_text()
        fields = raw[raw.rfind(")") + 2:].split()
        return fields[19] == start and fields[0] not in ("Z", "X")
    except (OSError, IndexError):
        return False


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux process-tree/ptrace contract")
class WatchdogContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="watchdog-contract-", dir=str(SCRATCH))
        self.root = Path(self.temporary.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.diag = self.root / "diagnostics"
        self.diag.mkdir()
        (self.root / "tmp").mkdir()
        self.env = os.environ.copy()
        self.env.update(PATH=str(self.bin), TMPDIR=str(self.root / "tmp"),
                        WATCHDOG_TEST_ROOT=str(self.root),
                        WATCHDOG_SECRET_SENTINEL="do-not-log-environment-9a410")
        self.processes = []
        self.invocations = 0

    def tearDown(self):
        # Failure cleanup belongs to the test, not the implementation under test.
        for proc in self.processes:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=3)
        for path in self.root.glob("*.pid.json"):
            identity = json.loads(path.read_text())
            if identity_alive(identity):
                try:
                    os.kill(identity[0], signal.SIGKILL)
                except ProcessLookupError:
                    pass
        self.temporary.cleanup()

    def script(self, name, body):
        path = self.root / (name + ".py")
        path.write_text(RECORD + "\n" + body)
        return path

    def command(self, target, timeout=.3, diagnostic_timeout=.25, args=()):
        return [sys.executable, str(RUNNER), "--timeout", str(timeout),
                "--diagnostic-timeout", str(diagnostic_timeout),
                "--diagnostic-dir", str(self.diag), "--", sys.executable,
                str(target)] + list(args)

    def launch(self, command):
        self.invocations += 1
        out = self.root / ("stdout-%d.log" % self.invocations)
        err = self.root / ("stderr-%d.log" % self.invocations)
        env = self.env.copy()
        env["WATCHDOG_TEST_STDERR"] = str(err)
        env["WATCHDOG_TEST_DIAGNOSTICS"] = str(self.diag)
        started = time.monotonic()
        with out.open("wb") as stdout, err.open("wb") as stderr:
            proc = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                    env=env, start_new_session=True)
        self.processes.append(proc)
        return proc, out, err, started

    def finish(self, launched, outer_timeout=12):
        proc, out, err, started = launched
        try:
            code = proc.wait(timeout=outer_timeout)
        except subprocess.TimeoutExpired:
            self.fail("watchdog exceeded independent bounded harness deadline")
        return code, out.read_text(errors="replace"), err.read_text(errors="replace"), time.monotonic() - started

    def run_target(self, target, **kwargs):
        return self.finish(self.launch(self.command(target, **kwargs)))

    def diagnostic_text(self):
        return "\n".join(p.read_text(errors="replace") for p in self.diag.rglob("*")
                         if p.is_file() and p.stat().st_size < 1024 * 1024)

    def identities(self, names):
        result = []
        for name in names:
            path = self.root / (name + ".pid.json")
            self.assertTrue(path.exists(), "fixture actually started: " + name)
            result.append(json.loads(path.read_text()))
        return result

    def assert_dead(self, names):
        identities = self.identities(names)
        # A dead zombie is no longer running. Allow only scheduler/reaping lag;
        # all termination work must already have been requested by the runner.
        deadline = time.monotonic() + .5
        while any(identity_alive(p) for p in identities) and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertFalse(any(identity_alive(p) for p in identities),
                         "runner left a live owned descendant: " + repr(identities))

    def wait_file(self, name, limit=3):
        path = self.root / name
        deadline = time.monotonic() + limit
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(.01)
        self.assertTrue(path.exists(), "fixture startup handshake: " + name)
        return path

    def debugger(self, name="gdb", mode="ok", child=False):
        worker = self.script("debugger_worker", r'''
record("debugger_child")
signal.signal(signal.SIGTERM, signal.SIG_IGN)
while True: time.sleep(.05)
''')
        path = self.bin / name
        code = RECORD + r'''
record("debugger")
prior = Path(os.environ["WATCHDOG_TEST_STDERR"]).read_text(errors="replace")
for f in Path(os.environ["WATCHDOG_TEST_DIAGNOSTICS"]).rglob("*"):
    if f.is_file() and f.stat().st_size < 1024 * 1024:
        prior += f.read_text(errors="replace")
commands = []
for i, arg in enumerate(sys.argv[1:], 1):
    if arg in ("-x", "--command") and i + 1 < len(sys.argv):
        commands.append(Path(sys.argv[i + 1]).read_text())
    elif arg.startswith("--command="):
        commands.append(Path(arg.split("=", 1)[1]).read_text())
event = dict(tool=Path(sys.argv[0]).name, argv=sys.argv[1:], commands=commands,
             started=time.monotonic(), timeout_seen="TIMEOUT" in prior,
             pid_seen=str(json.loads((root / "target.pid.json").read_text())[0]) in prior)
(root / "debugger-event.json").write_text(json.dumps(event))
print("SIMULATED_ALL_THREAD_STACKS", flush=True)
'''
        if child:
            code += "subprocess.Popen([sys.executable, %r], start_new_session=True)\n" % str(worker)
            code += "while not (root / 'debugger_child.pid.json').exists(): time.sleep(.005)\n"
        if mode == "recover":
            code += "os.kill(json.loads((root / 'target.pid.json').read_text())[0], signal.SIGUSR1)\n"
        elif mode == "fail":
            code += "print('SIMULATED_DEBUGGER_FAILURE', file=sys.stderr, flush=True)\nsys.exit(9)\n"
        elif mode == "hang":
            code += "signal.signal(signal.SIGTERM, signal.SIG_IGN)\nwhile True: time.sleep(.05)\n"
        path.write_text("#!" + sys.executable + "\n" + code)
        path.chmod(0o755)

    def sleeper(self, recover=False):
        body = 'record("target")\n'
        if recover:
            body += 'signal.signal(signal.SIGUSR1, lambda *args: sys.exit(0))\n'
        body += 'while True: time.sleep(.05)\n'
        return self.script("target", body)

    def tree(self, normal_exit=False):
        grandchild = self.script("grandchild", '''record("grandchild")
signal.signal(signal.SIGTERM, signal.SIG_IGN)
while True: time.sleep(.05)
''')
        child = self.script("child", '''record("child")
signal.signal(signal.SIGTERM, signal.SIG_IGN)
subprocess.Popen([sys.executable, %r], start_new_session=True)
while not (root / "grandchild.pid.json").exists(): time.sleep(.005)
(root / "tree-ready").write_text("ready")
while True: time.sleep(.05)
''' % str(grandchild))
        return self.script("target", '''record("target")
subprocess.Popen([sys.executable, %r])
while not (root / "tree-ready").exists(): time.sleep(.005)
%s
''' % (str(child), "sys.exit(7)" if normal_exit else "while True: time.sleep(.05)"))

    def test_01_normal_exit_output_argv_environment_no_shell(self):
        marker = self.root / "shell-must-not-run"
        args = ["two words", "", "; exit 99", "$(touch %s)" % marker,
                "quote'\"", "--timeout", "literal\nnewline", "unicode-á"]
        target = self.script("target", '''record("target")
print(json.dumps(sys.argv[1:], ensure_ascii=False), flush=True)
print(os.environ["WATCHDOG_SECRET_SENTINEL"], flush=True)
print("authored stderr", file=sys.stderr, flush=True)
sys.exit(7)
''')
        self.debugger()
        code, out, err, _ = self.run_target(target, timeout=3, args=args)
        self.assertEqual(code, 7)
        self.assertIn(json.dumps(args, ensure_ascii=False), out)
        self.assertIn(self.env["WATCHDOG_SECRET_SENTINEL"], out)
        self.assertIn("authored stderr", err)
        self.assertFalse(marker.exists())
        self.assertFalse((self.root / "debugger-event.json").exists())

    def test_02_success_and_signal_status(self):
        success = self.script("target", 'record("target")\nsys.exit(0)\n')
        self.assertEqual(self.run_target(success, timeout=3)[0], 0)
        killed = self.script("target", 'record("target")\nos.kill(os.getpid(), signal.SIGTERM)\n')
        self.assertEqual(self.run_target(killed, timeout=3)[0], 128 + signal.SIGTERM)

    def test_15_completion_text_and_available_debugger_do_not_hide_a_crash(self):
        self.debugger(mode="ok")  # A debugger exit zero is never target success.
        for signum in (signal.SIGABRT, signal.SIGSEGV):
            target = self.script("target", '''record("target")
import resource
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
print("native_egl_lifetime complete result=0", flush=True)
os.kill(os.getpid(), %d)
''' % signum)
            with self.subTest(signum=signum):
                code, out, _, _ = self.run_target(target, timeout=3)
                self.assertIn("native_egl_lifetime complete result=0", out)
                self.assertEqual(code, 128 + signum)
                self.assertFalse((self.root / "debugger-event.json").exists())

    def test_03_invalid_deadlines_never_start_target(self):
        target = self.script("target", 'record("target")\n')
        for field in ("timeout", "diagnostic_timeout"):
            for value in ("0", "-1", "nan", "inf", "-inf", "1e309", "not-a-number"):
                with self.subTest(field=field, value=value):
                    options = dict(timeout=1, diagnostic_timeout=1)
                    options[field] = value
                    code, _, _, _ = self.run_target(target, **options)
                    self.assertNotEqual(code, 0)
                    self.assertNotEqual(code, 124)
                    self.assertFalse((self.root / "target.pid.json").exists())

    def test_04_gdb_precedence_all_threads_and_timeout_latched_before_diagnosis(self):
        target = self.sleeper(recover=True)
        self.debugger("gdb", "recover")
        self.debugger("eu-stack", "fail")
        launched = self.launch(self.command(target, timeout=.3, diagnostic_timeout=.3))
        code, out, err, elapsed = self.finish(launched)
        self.assertEqual(code, 124, "debugger-induced successful exit cannot erase timeout")
        event = json.loads((self.root / "debugger-event.json").read_text())
        self.assertEqual(event["tool"], "gdb")
        self.assertGreaterEqual(event["started"] - launched[3], .29, "no attach before original deadline")
        self.assertTrue(event["timeout_seen"] and event["pid_seen"], "timeout/PID receipt must precede debugger launch")
        invocation = " ".join(event["argv"] + event["commands"])
        target_pid = self.identities(["target"])[0][0]
        self.assertRegex(invocation, r"(?<!\d)%d(?!\d)" % target_pid,
                         "diagnostic attaches the actual target PID")
        self.assertTrue(any(option in event["argv"] for option in ("--nx", "-nx", "-n")), "gdb must not execute init files")
        self.assertRegex(invocation, r"(?:thread\s+apply\s+all|t\s+a\s+a)")
        self.assertTrue("batch" in invocation or "set pagination off" in invocation)
        self.assertIn("SIMULATED_ALL_THREAD_STACKS", out + err + self.diagnostic_text())
        self.assertNotIn(self.env["WATCHDOG_SECRET_SENTINEL"], err + self.diagnostic_text())
        self.assertLess(elapsed, 10)
        self.assert_dead(["target", "debugger"])

    def test_05_eu_stack_fallback(self):
        self.debugger("eu-stack")
        code, out, err, _ = self.run_target(self.sleeper())
        self.assertEqual(code, 124)
        event = json.loads((self.root / "debugger-event.json").read_text())
        self.assertEqual(event["tool"], "eu-stack")
        self.assertRegex(" ".join(event["argv"]), r"(?<!\d)%d(?!\d)" % self.identities(["target"])[0][0])
        self.assertTrue(event["timeout_seen"])
        self.assertIn("SIMULATED_ALL_THREAD_STACKS", out + err + self.diagnostic_text())
        self.assert_dead(["target", "debugger"])

    def test_06_no_debugger_is_diagnosed_and_cleans_target(self):
        code, out, err, _ = self.run_target(self.sleeper())
        self.assertEqual(code, 124)
        receipt = out + err + self.diagnostic_text()
        self.assertIn("TIMEOUT", receipt)
        self.assertRegex(receipt.lower(), r"unavailable|not found|missing|not installed|no debugger|no stack")
        self.assert_dead(["target"])

    def test_07_debugger_failure_does_not_change_timeout(self):
        self.debugger(mode="fail")
        code, out, err, _ = self.run_target(self.sleeper())
        self.assertEqual(code, 124)
        self.assertIn("SIMULATED_DEBUGGER_FAILURE", out + err + self.diagnostic_text())
        self.assert_dead(["target", "debugger"])

    def test_08_stuck_debugger_and_both_process_trees_are_bounded(self):
        self.debugger(mode="hang", child=True)
        code, _, _, elapsed = self.run_target(self.tree(), timeout=.5, diagnostic_timeout=.25)
        self.assertEqual(code, 124)
        self.assertLess(elapsed, 10, "diagnostic timeout is separate and finite")
        self.assert_dead(["target", "child", "grandchild", "debugger", "debugger_child"])

    def test_09_normal_exit_reaps_own_remaining_descendants(self):
        code, _, _, _ = self.run_target(self.tree(normal_exit=True), timeout=3)
        self.assertEqual(code, 7)
        self.assert_dead(["target", "child", "grandchild"])

    def interrupted_target(self, sig):
        self.debugger()
        launched = self.launch(self.command(self.tree(), timeout=8))
        self.wait_file("tree-ready")
        launched[0].send_signal(sig)
        code, _, _, _ = self.finish(launched)
        self.assertNotEqual(code, 0)
        self.assertFalse((self.root / "debugger-event.json").exists(), "interrupt is not an early diagnostic attach")
        self.assert_dead(["target", "child", "grandchild"])

    def test_10_keyboard_interrupt_cleans_target_tree(self):
        self.interrupted_target(signal.SIGINT)

    def test_13_sigterm_cleans_target_tree(self):
        self.interrupted_target(signal.SIGTERM)

    def test_14_interrupt_during_diagnosis_cleans_debugger_tree(self):
        self.debugger(mode="hang", child=True)
        launched = self.launch(self.command(self.tree(), timeout=.5, diagnostic_timeout=5))
        self.wait_file("debugger_child.pid.json")
        launched[0].send_signal(signal.SIGINT)
        code, _, _, _ = self.finish(launched)
        self.assertNotEqual(code, 0)
        self.assert_dead(["target", "child", "grandchild", "debugger", "debugger_child"])

    def test_11_execution_diagnostics_are_unique_and_not_overwritten(self):
        self.debugger()
        target = self.sleeper()
        self.assertEqual(self.run_target(target)[0], 124)
        first = {p.relative_to(self.diag): p.read_bytes() for p in self.diag.rglob("*") if p.is_file()}
        dirs = {p.name for p in self.diag.iterdir() if p.is_dir()}
        self.assertEqual(len(dirs), 1, "one exclusive execution subdirectory")
        self.assertTrue(first)
        self.assertEqual(self.run_target(target)[0], 124)
        self.assertEqual(len([p for p in self.diag.iterdir() if p.is_dir()]), 2)
        for relative, contents in first.items():
            self.assertEqual((self.diag / relative).read_bytes(), contents)

    def test_12_cleanup_never_kills_callers_group_or_unrelated_sibling(self):
        target = self.sleeper()
        sentinel = self.script("sentinel", 'record("external_sentinel")\nwhile True: time.sleep(.05)\n')
        command = self.command(target)
        # The isolated caller and its sentinel share their group with the
        # watchdog. They are external to the watchdog's descendant tree.
        caller = self.script("caller", '''record("caller")
sentinel = subprocess.Popen([sys.executable, %r])
try:
    while not (root / "external_sentinel.pid.json").exists(): time.sleep(.005)
    result = subprocess.run(%r, timeout=10)
    (root / "caller-result.json").write_text(json.dumps(dict(code=result.returncode, sentinel_alive=sentinel.poll() is None)))
finally:
    sentinel.terminate()
    sentinel.wait(timeout=3)
''' % (str(sentinel), command))
        launched = self.launch([sys.executable, str(caller)])
        code, _, _, _ = self.finish(launched)
        self.assertEqual(code, 0, "watchdog killed or stranded its caller group")
        result = json.loads((self.root / "caller-result.json").read_text())
        self.assertEqual(result["code"], 124)
        self.assertTrue(result["sentinel_alive"], "external sibling must survive runner cleanup")
        self.assert_dead(["target", "external_sentinel", "caller"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--temporary-root", type=Path, required=True)
    options, unittest_args = parser.parse_known_args()
    RUNNER = options.runner.resolve()
    SCRATCH = options.temporary_root.resolve()
    SCRATCH.mkdir(parents=True, exist_ok=True)
    if not RUNNER.is_file():
        parser.error("runner does not exist: " + str(RUNNER))
    unittest.main(argv=[sys.argv[0]] + unittest_args)
