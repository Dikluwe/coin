#!/usr/bin/env python3
"""Audit cold program creations from process startup through the first apply."""

import argparse
import os
import re
import subprocess
import sys


PREPARE_PREFIX = "COIN_RENDER_PHASE bgfx_program_prepare "
PREPARE = re.compile(
    r"^COIN_RENDER_PHASE bgfx_program_prepare role=([a-z_]+) samples=([0-9]+)\s*$"
)
EXPECTED = {
    "bump": {"bump_receiver"},
    "ordinary": {"shadow_multitexture"},
    "mixed": {"bump_receiver", "shadow_multitexture"},
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=sorted(EXPECTED), required=True)
    parser.add_argument("--samples", type=int, choices=(1, 2, 4), required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command or "--cold-" + args.profile not in command:
        parser.error("command must invoke the matching real --cold-PROFILE focal")
    try:
        sample_index = command.index("--samples")
        child_samples = int(command[sample_index + 1])
    except (ValueError, IndexError):
        parser.error("child command must explicitly retain --samples N")
    if child_samples != args.samples:
        parser.error("child samples must equal the program-audit samples")

    environment = os.environ.copy()
    environment["COIN_RENDER_TRACE_PHASES"] = "1"
    # Do not create targets, warm caches, repeat the command, or impose a second
    # deadline. CTest's unchanged 180-second limit covers this entire process
    # and its single child, including all original functional checks.
    try:
        child = subprocess.Popen(
            command, env=environment, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=1,
        )
    except OSError as error:
        print("first-program audit could not start child: " + str(error), file=sys.stderr)
        return 1

    begun = False
    finished = False
    roles = set()
    errors = []
    try:
        for line in child.stdout:
            # Preserve all functional evidence and flush diagnostics while a
            # cold pipeline is being compiled. One pipe preserves event order.
            sys.stdout.write(line)
            sys.stdout.flush()
            if not finished:
                # Initialization/prepareBackend belongs to the cold first
                # frame too. Moving an unused program before apply_begin must
                # not evade the exact expected-role assertion.
                if line.startswith(PREPARE_PREFIX):
                    event = PREPARE.fullmatch(line)
                    if event is None:
                        errors.append("malformed program-creation event: " + line.rstrip())
                    else:
                        role, samples = event.groups()
                        roles.add(role)
                        if int(samples) != args.samples:
                            errors.append("program event has wrong requested sample count")
                if not begun and line.startswith("bump_shadow_apply_begin phase="):
                    begun = True
                    if not re.search(r" serial=0 root_submits=0\s*$", line):
                        errors.append("first apply was not an unsubmitted cold target")
                elif begun and line.startswith("bump_shadow phase="):
                    finished = True
        return_code = child.wait()
    except BaseException:
        child.terminate()
        child.wait()
        raise
    finally:
        child.stdout.close()

    if return_code == 77:
        print("first-program audit: child explicitly skipped unavailable environment")
        return 77
    if return_code != 0:
        print("first-program audit: functional child failed, rc=" + str(return_code),
              file=sys.stderr)
        return return_code if 0 < return_code < 256 else 1
    if not begun or not finished:
        errors.append("missing complete first-apply phase interval")
    if roles != EXPECTED[args.profile]:
        errors.append("first-apply roles expected=" + repr(sorted(EXPECTED[args.profile]))
                      + " actual=" + repr(sorted(roles)))
    print("first-program audit profile=" + args.profile
          + " samples=" + str(args.samples) + " roles=" + repr(sorted(roles)))
    for error in errors:
        print("first-program audit FAIL: " + error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
