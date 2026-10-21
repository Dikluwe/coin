#!/usr/bin/env python3
"""Audit real cold fixed-program creations while retaining the functional child."""
import argparse
import os
import re
import subprocess
import sys
import time
import unittest

PREPARE = re.compile(r"^COIN_RENDER_PHASE bgfx_program_prepare role=([a-z_]+) samples=([0-9]+)\s*$")
PORTABLE_SUBMIT = re.compile(
    r"^COIN_RENDER_PHASE bgfx_portable_program_submit family=([a-z_]+) program=(0[xX][0-9a-fA-F]+|[0-9]+) samples=([0-9]+)\s*$"
)
PORTABLE_FAMILIES = {"base", "native_depth", "sample_depth", "shadow_base",
                     "shadow_sample", "peel", "shadow_peel", "oit", "shadow_oit"}
COMPILE = re.compile(r"^COIN_RENDER_PHASE portable_shader_compile module=(\S+) profile=(\S+)\s*$")
BEGIN = re.compile(r"^portable_preparation_first_begin profile=(\S+) samples=([124]) serial=0 entries=0\s*$")
END = re.compile(r"^portable_preparation_first_done profile=(\S+) samples=([124]) status=0 serial=([1-9][0-9]*) entries=1\s*$")
EXPECTED = {"module": set(), "primary": set(), "shadow": set(),
            "mixed": {"general_surface"}, "ordinary": {"general_surface"},
            "bump-mixed": {"legacy_bump"},
            "occurrence-legacy": {"legacy_bump"},
            "occurrence-extended": {"bump_receiver"},
            "occurrence-shadow": {"bump_receiver"}}


class ColdAudit:
    def __init__(self, profile, samples):
        self.profile, self.samples = profile, samples
        self.begun = self.finished = False
        self.roles, self.compiles, self.errors = set(), [], []
        self.fixed_depth = 0
        self.portable_submits = []

    def consume(self, line):
        if self.finished:
            return
        # Includes query/prepareBackend: moving an unwanted executor out of
        # the apply bracket must not turn the same cold process into a pass.
        if line.startswith("COIN_RENDER_PHASE bgfx_program_prepare "):
            event = PREPARE.fullmatch(line)
            if event is None:
                self.errors.append("malformed real program-creation event")
            else:
                role, samples = event.groups()
                self.roles.add(role)
                if int(samples) != self.samples:
                    self.errors.append("creation event uses wrong target sample count")
        if line.startswith("COIN_RENDER_PHASE portable_shader_compile "):
            event = COMPILE.fullmatch(line)
            if event is None:
                self.errors.append("malformed real portable compilation event")
            else:
                self.compiles.append(event.groups())
        if line.startswith("COIN_RENDER_PHASE bgfx_portable_program_submit"):
            event = PORTABLE_SUBMIT.fullmatch(line)
            if event is None:
                self.errors.append("malformed actual portable-program submit event")
            else:
                family, handle_text, samples_text = event.groups()
                handle = int(handle_text, 16 if handle_text.lower().startswith("0x") else 10)
                if family not in PORTABLE_FAMILIES:
                    self.errors.append("unknown actual portable submit family")
                # BGFX ProgramHandle.idx is uint16_t; 0 is valid, 0xffff invalid.
                if not 0 <= handle < 0xffff:
                    self.errors.append("portable submit has invalid actual program handle")
                if int(samples_text) != self.samples:
                    self.errors.append("actual portable submit has wrong target sample count")
                if not self.begun:
                    self.errors.append("portable submit predates the first scene apply")
                self.portable_submits.append((family, handle, int(samples_text)))
        if line.startswith("COIN_RENDER_PHASE bgfx_surface_depth_program "):
            self.fixed_depth += 1
        if line.startswith("portable_preparation_first_begin "):
            event = BEGIN.fullmatch(line)
            if self.begun or event is None or event.groups() != (self.profile, str(self.samples)):
                self.errors.append("first begin is missing, duplicated or not serial0/entries0")
            self.begun = True
        elif line.startswith("portable_preparation_first_done "):
            event = END.fullmatch(line)
            if not self.begun or event is None or event.groups()[:2] != (self.profile, str(self.samples)):
                self.errors.append("first done lacks one successful cold logical publication")
            self.finished = True

    def result(self, baseline=False):
        errors = list(self.errors)
        if not self.begun or not self.finished:
            errors.append("incomplete cold first-apply interval")
        # Compilation events prove real module use alongside the independent
        # GPU RGB/depth/Core/FFI tests; registration alone is insufficient.
        if bool(self.compiles) != (self.profile != "ordinary"):
            errors.append("first portable compilation presence contradicts consumed occurrences")
        if self.profile == "ordinary" and self.portable_submits:
            errors.append("ordinary-only first frame submitted a portable program")
        if self.profile != "ordinary" and not self.portable_submits and not baseline:
            errors.append("no actual portable program submitted before first publication")
        # Old baseline binaries have no new submit event. Only its absence is
        # exempted; malformed/wrong/ordinary events remain invalid in baseline.
        if not baseline:
            if self.roles != EXPECTED[self.profile]:
                errors.append("fixed roles expected=" + repr(sorted(EXPECTED[self.profile]))
                              + " actual=" + repr(sorted(self.roles)))
            if not EXPECTED[self.profile] and self.fixed_depth:
                errors.append("module-only frame executed a fixed surface-depth route")
        return errors


class AuditTests(unittest.TestCase):
    def audit(self, profile="module", samples=2, early=(), late=(), submits=None):
        a = ColdAudit(profile, samples)
        for line in early:
            a.consume(line + "\n")
        a.consume("portable_preparation_first_begin profile=%s samples=%d serial=0 entries=0\n" % (profile, samples))
        if profile != "ordinary":
            a.consume("COIN_RENDER_PHASE portable_shader_compile module=stable profile=opaque\n")
        if submits is None:
            submits = () if profile == "ordinary" else (
                "COIN_RENDER_PHASE bgfx_portable_program_submit family=native_depth program=0 samples=%d" % samples,)
        for line in submits:
            a.consume(line + "\n")
        a.consume("portable_preparation_first_done profile=%s samples=%d status=0 serial=1 entries=1\n" % (profile, samples))
        for line in late:
            a.consume(line + "\n")
        return a

    def test_module_without_fixed(self):
        self.assertFalse(self.audit().result())

    def test_early_unwanted_not_hidden(self):
        a = self.audit(early=("COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2",))
        self.assertTrue(a.result())
        self.assertFalse(a.result(baseline=True))

    def test_bump_mixed_requires_actual_legacy_family(self):
        self.assertTrue(self.audit("bump-mixed").result())
        self.assertFalse(self.audit("bump-mixed", early=(
            "COIN_RENDER_PHASE bgfx_program_prepare role=legacy_bump samples=2",)).result())
        self.assertTrue(self.audit("bump-mixed", early=(
            "COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2",)).result())
        self.assertTrue(self.audit("bump-mixed", early=(
            "COIN_RENDER_PHASE bgfx_program_prepare role=legacy_bump samples=2",
            "COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2")).result())

    def test_mixed_requires_fixed(self):
        self.assertTrue(self.audit("mixed").result())
        self.assertFalse(self.audit("mixed", early=("COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2",)).result())

    def test_ordinary_has_no_portable_compile(self):
        self.assertFalse(self.audit("ordinary", early=("COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2",)).result())

    def test_later_legitimate_executors_ignored(self):
        self.assertFalse(self.audit(late=("COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2",)).result())

    def test_fixed_depth_not_hidden(self):
        self.assertTrue(self.audit(early=("COIN_RENDER_PHASE bgfx_surface_depth_program family=general mode=native samples=2",)).result())

    def test_wrong_samples_reject(self):
        self.assertTrue(self.audit("mixed", early=("COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=4",)).result())

    def test_unknown_role_reject(self):
        self.assertTrue(self.audit(early=("COIN_RENDER_PHASE bgfx_program_prepare role=renamed_executor samples=2",)).result())

    def test_missing_publication_reject(self):
        a = ColdAudit("module", 2)
        a.consume("portable_preparation_first_begin profile=module samples=2 serial=0 entries=0\n")
        self.assertTrue(a.result())

    def test_non_cold_or_failed_publication_reject(self):
        a = ColdAudit("module", 2)
        a.consume("portable_preparation_first_begin profile=module samples=2 serial=7 entries=1\n")
        a.consume("portable_preparation_first_done profile=module samples=2 status=4 serial=7 entries=1\n")
        self.assertTrue(a.result())


    def test_missing_actual_submit_only_baseline_exempt(self):
        a = self.audit(submits=())
        self.assertTrue(a.result())
        self.assertFalse(a.result(baseline=True))

    def test_actual_submit_handle_zero_and_max_valid(self):
        for handle in ("0", "65534", "0xfffe"):
            a = self.audit(submits=("COIN_RENDER_PHASE bgfx_portable_program_submit family=sample_depth program=%s samples=2" % handle,))
            self.assertFalse(a.result())

    def test_all_declared_submit_families(self):
        for family in PORTABLE_FAMILIES:
            a = self.audit(submits=("COIN_RENDER_PHASE bgfx_portable_program_submit family=%s program=7 samples=2" % family,))
            self.assertFalse(a.result())

    def test_actual_submit_invalid_handle(self):
        for handle in ("65535", "65536", "0xffff", "-1"):
            a = self.audit(submits=("COIN_RENDER_PHASE bgfx_portable_program_submit family=base program=%s samples=2" % handle,))
            self.assertTrue(a.result())
            self.assertTrue(a.result(baseline=True))

    def test_actual_submit_wrong_samples(self):
        a = self.audit(submits=("COIN_RENDER_PHASE bgfx_portable_program_submit family=base program=7 samples=4",))
        self.assertTrue(a.result())
        self.assertTrue(a.result(baseline=True))

    def test_actual_submit_unknown_family(self):
        a = self.audit(submits=("COIN_RENDER_PHASE bgfx_portable_program_submit family=renamed program=7 samples=2",))
        self.assertTrue(a.result())
        self.assertTrue(a.result(baseline=True))

    def test_actual_submit_malformed(self):
        for event in ("COIN_RENDER_PHASE bgfx_portable_program_submit",
                      "COIN_RENDER_PHASE bgfx_portable_program_submit family=base samples=2",
                      "COIN_RENDER_PHASE bgfx_portable_program_submit family=base program=7 samples=2 ignored=1"):
            a = self.audit(submits=(event,))
            self.assertTrue(a.result())
            self.assertTrue(a.result(baseline=True))

    def test_ordinary_must_not_submit_portable(self):
        a = self.audit("ordinary", early=("COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2",),
                       submits=("COIN_RENDER_PHASE bgfx_portable_program_submit family=base program=7 samples=2",))
        self.assertTrue(a.result())
        self.assertTrue(a.result(baseline=True))

    def test_early_submit_cannot_fake_scene_execution(self):
        a = self.audit(early=("COIN_RENDER_PHASE bgfx_portable_program_submit family=base program=7 samples=2",))
        self.assertTrue(a.result())

    def test_later_submit_is_outside_first_interval(self):
        a = self.audit("ordinary", early=("COIN_RENDER_PHASE bgfx_program_prepare role=general_surface samples=2",),
                       late=("COIN_RENDER_PHASE bgfx_portable_program_submit family=base program=7 samples=2",))
        self.assertFalse(a.result())


def main():
    if sys.argv[1:] == ["--self-test"]:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(AuditTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=sorted(EXPECTED), required=True)
    parser.add_argument("--samples", type=int, choices=(1, 2, 4), required=True)
    parser.add_argument("--baseline", action="store_true")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    try:
        mode = command[command.index("--mode") + 1]
        samples = int(command[command.index("--samples") + 1])
    except (ValueError, IndexError):
        parser.error("child must explicitly select --mode and --samples")
    if mode != "prepare-" + args.profile or samples != args.samples:
        parser.error("child mode/sample arguments must match the cold audit")
    env = os.environ.copy()
    env["COIN_RENDER_TRACE_PHASES"] = "1"
    # Exactly one real child, no warmup/retry/new timeout. CTest owns the
    # unchanged 180 seconds for initialization, first apply and all transitions.
    process_start = time.monotonic()
    first_publication_ms = None
    try:
        child = subprocess.Popen(command, env=env, stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, bufsize=1)
    except OSError as error:
        print("portable preparation cannot start child: " + str(error), file=sys.stderr)
        return 1
    audit = ColdAudit(args.profile, args.samples)
    try:
        for line in child.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            audit.consume(line)
            if audit.finished and first_publication_ms is None:
                first_publication_ms = (time.monotonic() - process_start) * 1000
                print("portable_preparation_first_publication_ms=" + str(first_publication_ms), flush=True)
        code = child.wait()
    except BaseException:
        child.terminate()
        child.wait()
        raise
    finally:
        child.stdout.close()
    if code == 77:
        return 77
    if code:
        print("portable preparation functional child failed rc=" + str(code), file=sys.stderr)
        return code if 0 < code < 256 else 1
    errors = audit.result(args.baseline)
    print("portable_preparation_audit profile=" + args.profile + " samples=" + str(args.samples)
          + " baseline=" + str(args.baseline) + " roles=" + repr(sorted(audit.roles))
          + " process_ms=" + str((time.monotonic() - process_start) * 1000)
          + " first_publication_ms=" + str(first_publication_ms)
          + " portable_submits=" + repr(audit.portable_submits)
          + " fixed_depth=" + str(audit.fixed_depth) + " module_compiles=" + str(len(audit.compiles)))
    for error in errors:
        print("portable preparation FAIL: " + error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
