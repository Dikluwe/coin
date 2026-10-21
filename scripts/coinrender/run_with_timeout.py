#!/usr/bin/env python3
"""Run one command with a functional deadline and bounded post-timeout stacks."""

import argparse
import ctypes
import datetime
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time


class Interrupted(BaseException):
    def __init__(self, signum):
        self.signum = signum


def positive_seconds(value):
    try:
        seconds = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected positive finite seconds")
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("expected positive finite seconds")
    return seconds


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", required=True, type=positive_seconds)
    parser.add_argument("--diagnostic-timeout", required=True, type=positive_seconds)
    parser.add_argument("--diagnostic-dir", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.command or args.command[0] != "--" or len(args.command) == 1:
        parser.error("supply -- PROGRAM [ARGS...]")
    args.command = args.command[1:]
    if os.name != "posix":
        parser.error("this watchdog requires POSIX process groups")
    return args


class Events:
    def __init__(self, directory):
        Path(directory).mkdir(parents=True, exist_ok=True)
        self.directory = Path(tempfile.mkdtemp(prefix="run-", dir=directory))
        self.stream = (self.directory / "events.log").open("w", buffering=1)

    def write(self, message):
        timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
        line = "{} monotonic={:.6f} {}\n".format(timestamp, time.monotonic(), message)
        # Neither argv nor the environment is logged: either may contain secrets.
        try:
            self.stream.write(line)
            self.stream.flush()
        except OSError:
            pass
        try:
            sys.stderr.write(line)
            sys.stderr.flush()
        except OSError:
            pass

    def close(self):
        self.stream.close()


def linux_setup(events):
    """Keep orphaned children ours, and authorize only this watchdog's tree."""
    if not sys.platform.startswith("linux"):
        return None
    libc = ctypes.CDLL(None, use_errno=True)
    prctl = libc.prctl
    prctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_ulong,
                     ctypes.c_ulong, ctypes.c_ulong]
    prctl.restype = ctypes.c_int
    # PR_SET_CHILD_SUBREAPER: daemonized grandchildren remain discoverable even
    # if their intermediate parent exits before our next process-table sample.
    if prctl(36, 1, 0, 0, 0) != 0:
        raise OSError(ctypes.get_errno(), "cannot enable child subreaper")
    parent_pid = os.getpid()

    def allow_parent_tracing():
        # PR_SET_PTRACER accepts one PID, including that tracer's descendants.
        # This executes only in the target child, never in the invoking shell.
        if prctl(0x59616D61, parent_pid, 0, 0, 0) != 0:
            os.write(2, b"watchdog: restricted ptrace opt-in unavailable\n")

    return allow_parent_tracing


def process_table():
    """pid -> (parent, group, birth identity, state), without command/env data."""
    result = {}
    if sys.platform.startswith("linux"):
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                # comm may itself contain spaces or parentheses.
                raw = (entry / "stat").read_text(errors="replace")
                fields = raw[raw.rfind(")") + 2:].split()
                result[int(entry.name)] = (int(fields[1]), int(fields[2]),
                                           fields[19], fields[0])
            except (OSError, ValueError, IndexError):
                continue
    else:
        # Non-Linux POSIX still gets process-group and live descendant cleanup.
        output = subprocess.check_output(
            ["ps", "-axo", "pid=,ppid=,pgid=,stat="], text=True, timeout=0.2)
        for line in output.splitlines():
            fields = line.split()
            if len(fields) == 4:
                pid, parent, group = map(int, fields[:3])
                result[pid] = (parent, group, None, fields[3])
    return result


class OwnedProcesses:
    def __init__(self):
        self.parent = os.getpid()
        self.caller_group = os.getpgrp()
        self.known = {}

    def snapshot(self):
        table = process_table()
        owned = {self.parent}
        # Entries retained from earlier snapshots also identify descendants
        # which left their original process group. Linux birth IDs prevent a
        # recycled PID from being mistaken for an owned process.
        for pid, birth in self.known.items():
            if pid in table and table[pid][2] == birth:
                owned.add(pid)
        changed = True
        while changed:
            changed = False
            for pid, info in table.items():
                if pid not in owned and info[0] in owned:
                    owned.add(pid)
                    changed = True
        owned.discard(self.parent)
        current = {pid: table[pid] for pid in owned if pid in table}
        self.known = {pid: info[2] for pid, info in current.items()}
        return current

    def send(self, signum):
        current = self.snapshot()
        # Only groups led by an owned process are signalled as groups. Never
        # send to our own group, or to a group inherited from the caller.
        groups = {info[1] for pid, info in current.items()
                  if pid == info[1] and info[1] != self.caller_group}
        for group in groups:
            try:
                os.killpg(group, signum)
            except ProcessLookupError:
                pass
        # Also cover descendants which created a separate group/session.
        for pid, info in current.items():
            if info[3].startswith("Z"):
                continue
            try:
                os.kill(pid, signum)
            except ProcessLookupError:
                pass

    def cleanup(self, events):
        # The independent two-second cleanup budget fits after the diagnostic
        # deadline; it never becomes an extension of the functional deadline.
        for signum, duration in ((signal.SIGTERM, 0.5), (signal.SIGKILL, 1.5)):
            deadline = time.monotonic() + duration
            while True:
                try:
                    self.send(signum)
                    reap_children()
                    if not self.snapshot():
                        return
                except (OSError, subprocess.SubprocessError) as error:
                    events.write("CLEANUP_ERROR {}".format(type(error).__name__))
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                time.sleep(min(0.02, remaining))
        events.write("CLEANUP_LIMIT reached; unkillable processes may remain")


def reap_children():
    while True:
        try:
            pid, unused_status = os.waitpid(-1, os.WNOHANG)
        except ChildProcessError:
            return
        if pid == 0:
            return


def wait_until(process, deadline, owned):
    while True:
        result = process.poll()
        if result is not None:
            return result
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        owned.snapshot()
        try:
            return process.wait(timeout=min(0.05, remaining))
        except subprocess.TimeoutExpired:
            pass


def collect_stacks(target, seconds, events, owned):
    debugger = shutil.which("gdb")
    if debugger:
        command = [debugger, "-nx", "-nh", "-batch",
                   "-iex", "set auto-load off", "-ex", "set pagination off",
                   "-ex", "set confirm off", "-ex", "thread apply all bt full",
                   "-ex", "detach", "--pid", str(target.pid)]
    else:
        debugger = shutil.which("eu-stack")
        command = [debugger, "-p", str(target.pid)] if debugger else None
    if command is None:
        events.write("DIAGNOSTIC_UNAVAILABLE neither gdb nor eu-stack found in PATH")
        return
    events.write("DIAGNOSTIC_START tool={} pid={} timeout_seconds={}".format(
        Path(debugger).name, target.pid, seconds))
    deadline = time.monotonic() + seconds
    try:
        with (events.directory / "stack.log").open("wb") as output:
            diagnostic = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                          stdout=output, stderr=subprocess.STDOUT,
                                          start_new_session=True)
            status = wait_until(diagnostic, deadline, owned)
            if status is None:
                events.write("DIAGNOSTIC_TIMEOUT pid={}".format(diagnostic.pid))
            else:
                events.write("DIAGNOSTIC_EXIT status={}".format(status))
    except OSError as error:
        events.write("DIAGNOSTIC_ERROR {}".format(type(error).__name__))
    # The caller now cleans both trees, including a timed-out debugger's
    # children. Target recovery during attach cannot change the verdict.


def main():
    args = arguments()
    events = Events(args.diagnostic_dir)
    owned = OwnedProcesses()
    previous_handlers = {}
    timed_out = False
    result = 125

    def interrupted(signum, unused_frame):
        raise Interrupted(signum)

    try:
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            previous_handlers[signum] = signal.signal(signum, interrupted)
        trace_setup = linux_setup(events)
        started = time.monotonic()
        target = subprocess.Popen(args.command, start_new_session=True,
                                  preexec_fn=trace_setup)
        events.write("START pid={} timeout_seconds={} diagnostic_dir={}".format(
            target.pid, args.timeout, events.directory))
        status = wait_until(target, started + args.timeout, owned)
        if status is None:
            timed_out = True
            result = 124
            events.write("TIMEOUT pid={} timeout_seconds={}".format(target.pid, args.timeout))
            collect_stacks(target, args.diagnostic_timeout, events, owned)
        else:
            result = status if status >= 0 else 128 - status
            events.write("EXIT pid={} status={}".format(target.pid, result))
    except (Interrupted, KeyboardInterrupt) as error:
        signum = error.signum if isinstance(error, Interrupted) else signal.SIGINT
        result = 128 + signum
        events.write("INTERRUPTED signal={}".format(signum))
    except OSError as error:
        if isinstance(error, FileNotFoundError):
            result = 127
        elif isinstance(error, PermissionError):
            result = 126
        events.write("ERROR {} errno={}".format(type(error).__name__, error.errno))
    finally:
        # A second interruption cannot abandon cleanup of our process trees.
        for signum in previous_handlers:
            signal.signal(signum, signal.SIG_IGN)
        try:
            owned.cleanup(events)
        finally:
            for signum, handler in previous_handlers.items():
                signal.signal(signum, handler)
            events.close()
    return 124 if timed_out else result


if __name__ == "__main__":
    sys.exit(main())
