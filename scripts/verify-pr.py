#!/usr/bin/env python3
"""Deterministic pre-verification checks for an OpenScanner PR.

Usage:
    scripts/verify-pr.py <pr-number> [--build] [--no-build]
    scripts/verify-pr.py --tier <pr-number>      # print: script | agent
    scripts/verify-pr.py --selftest              # assert the tier table and the fork-log rule (CI runs this)
    scripts/verify-pr.py --staged                # pre-commit mode: check the staged set only (the git hook calls this)

Works on git refs (no checkout required): fetches `refs/pull/<n>/head` and the
PR's base branch, then checks the diff. Exit 0 = all gating checks passed;
1 = a gating check failed; 2 = environment problem.

Tiers (printed with the report; used by verdict-gate.yml to decide whether a PR
is script-gated or needs an agent verdict):
    agent tier   engine/**, tools/**, mesh/**, ui/**, scripts/**, tests/hardware/**,
                 any CMakeLists.txt, .github/**, .claude/**, opencode.json(c),
                 docs/recon/**, docs/recomp/{WINDOWS-FORK,FORK-COORDINATION,ARCHITECTURE}.md,
                 tests/results/** (records, logs, addenda — #21)
    script tier  everything else (PLAN.md, docs/recomp/** except the documents listed above, CLAUDE.md, README.md, ...)
    An empty or unreadable file list is treated as agent tier. The list is
    pinned by `--selftest`; edit the table and the patterns together.

Gating checks
  whitespace      `git diff --check` (the same rule `scripts/git-hooks/pre-commit`
                  runs on the staged set via `--staged`)
  run-records     tests/results/: no existing record modified, deleted or renamed
                  in place (addenda *-correction.md / *-analysis.md / *-addendum.md
                  and `_`-prefixed support files excepted)
  record-provenance
                  an added tests/results/*.md record must be accompanied, in the
                  same PR, by raw evidence: a file whose name starts with the
                  record's stem, **or** a raw artifact the record cites in
                  backticks (so multi-condition runs can name their logs
                  `<slug>-baseline.log` / `<slug>-enabled.log`). Addenda,
                  corrections, analyses and `_`-prefixed support files are exempt.
  plan            PLAN.md row syntax: unique numbers, valid statuses, well-formed.
                  Status moves and new rows are reported, not gated.
  archives        cited `sha256 <hex>` values in changed tests/results/*.md must
                  match a file named next to the citation in the PR; a citation
                  that cannot be resolved fails. Added *.ips files are compared
                  against ~/Library/Logs/DiagnosticReports/<same basename> when
                  that directory exists (local best-effort; CI has no such dir).
  fork-log        a PR that moves engine/include/openscanner/obscan_api.h must also
                  add its line to docs/recomp/FORK-COORDINATION.md §5 — the agreed
                  routine (one line per header change, in the same PR); the fork's
                  --pincheck is the drift alarm.
  hook            .claude/hooks/guard-hw-run.sh is executable, and the head hook
                  satisfies the guard invariants (must-block / must-allow cases
                  below). A base-vs-head probe matrix diff is reported.
  ui-js-syntax    changed ui/*.js files must parse (`node --check`) — the
                  #54 class: a parse error kills the whole page.
  build           cmake configure + build + ctest -LE hardware; runs only when
                  the working tree is the PR head or --build is passed (CI runs
                  it from the head checkout). When ctest fails, its captured
                  output (the FAILED block and the per-test details) is printed
                  before the report, so the CI log names the tests (row #185);
                  the exit code alone still decides the check.

Reported only: tier, inventory (files not mentioned in the PR body), header
(comment-only vs code change in obscan_api.h), modes, hook probe matrix diff.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

REPO = "juicie-brucie/Open3DScanner"
STATUSES = {"TODO", "RECON", "SPEC-READY", "BUILD", "HW", "DONE", "BLOCKED"}

WT_HW = "/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw"
MAIN = "/Users/brucemackay/DevReposiMac/Open3DScanner"
WT_VERIFY = "/Users/brucemackay/DevReposiMac/Open3DScanner-wt-verify"
DEVICE_TEST = "./build/tests/hardware/device_message_listener_test/device_message_listener_test"
DATA_TEST = "./build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test"

# Paths whose review needs judgment (semantics, prose claims, or the gate itself).
AGENT_PATTERNS = [
    r"^engine/", r"^tools/", r"^mesh/", r"^ui/", r"^scripts/",
    r"^tests/hardware/", r"CMakeLists\.txt$",
    r"^\.github/", r"^\.claude/",
    # The config that steers every agent in this repo (permissions, compaction,
    # MCP): script-tier would let it auto-merge with no judgment pass (rev 14's
    # H4 finding — verified against this script).
    r"^opencode\.jsonc?$",
    r"^docs/recon/", r"^docs/architecture/",
    # Contract-bearing docs under the otherwise script-tier docs/recomp/:
    # the Windows-fork charter and coordination file carry rules (the contract
    # pin, the two-writer rule) and deserve a verdict like docs/recon/**.
    r"^docs/recomp/(WINDOWS-FORK|FORK-COORDINATION)\.md$",
    # The doc that states the roles' walls and the clean-room principle: like the fork
    # documents, it carries rules rather than prose, so it earns a verdict.
    r"^docs/recomp/ARCHITECTURE\.md$",
    # Every evidence artifact under tests/results/** — records, logs and addenda —
    # is agent-tier (#21): the directory is the loop's highest-stakes artifact
    # class, and an added record auto-merging unreviewed was the hole.
    r"^tests/results/",
]


def is_agent_tier(path):
    return any(re.search(p, path) for p in AGENT_PATTERNS)


def fork_log_problem(changed_paths):
    """The agreed routine with the fork lane: a PR that moves the contract header adds its
    §5 line to the coordination file in the same PR. The fork's `--pincheck` is the drift
    alarm; the log is the revision trail, and this keeps it complete without a message per
    move. Returns the problem string, or None when the PR is fine."""
    hdr = "engine/include/openscanner/obscan_api.h"
    forklog = "docs/recomp/FORK-COORDINATION.md"
    if hdr in changed_paths and forklog not in changed_paths:
        return (f"{hdr} moved without a line in {forklog} §5 — the agreed routine is one "
                "line per header change, in the same PR")
    return None


# The tier list is load-bearing: a path that should be agent-tier returning
# `script` lets auto-verdict post success and the PR merges judged by nothing.
# This table pins the list; CI runs `--selftest` before trusting a tier
# (verify.yml: base and PR copies; verdict-gate.yml: before the tier decision).
# Edit the table and the patterns together — a mismatch fails the run.
TIER_CASES = [
    ("engine/src/ws_broadcast_server.cpp", "agent"),
    ("engine/include/openscanner/obscan_api.h", "agent"),
    ("tools/scan_server/main.cpp", "agent"),
    ("mesh/src/kernel.cpp", "agent"),
    ("ui/app.js", "agent"),
    ("scripts/verify-pr.py", "agent"),
    ("tests/hardware/plan56-stall-and-v5-visual.md", "agent"),
    ("docs/recon/findings/scan-live-data.md", "agent"),
    ("docs/architecture/scan-viewer.md", "agent"),
    ("docs/recomp/WINDOWS-FORK.md", "agent"),
    ("docs/recomp/FORK-COORDINATION.md", "agent"),
    ("docs/recomp/ARCHITECTURE.md", "agent"),
    ("tests/results/2026-10-18-run.md", "agent"),
    ("tests/results/2026-10-18-run.log", "agent"),
    ("tests/results/2026-10-18-run.ips", "agent"),
    ("tests/results/2026-10-18-old-addendum.md", "agent"),
    (".github/workflows/verify.yml", "agent"),
    (".claude/hooks/guard-hw-run.sh", "agent"),
    ("opencode.json", "agent"),
    ("opencode.jsonc", "agent"),
    ("CMakeLists.txt", "agent"),
    ("docs/recomp/PLAN.md", "script"),
    ("docs/recomp/ROADMAP.md", "script"),
    ("docs/recomp/log/2026-10-18-entry.md", "script"),
    ("CLAUDE.md", "script"),
    ("README.md", "script"),
    ("docs/research/plane-removal-reverse-engineering.md", "script"),
    ("docs/tooling/rea-ghidra-setup.md", "script"),
]


def is_support_file(path):
    """tests/results/_TEMPLATE.md and friends: they describe how to run, they are
    not records — exempt from the in-place rule and from provenance."""
    return os.path.basename(path).startswith("_")


def is_run_record(path):
    """An added tests/results/*.md is a run record unless it is an addendum or an
    underscore-prefixed support file (_TEMPLATE.md, _NOTES.md, ...). Support files
    describe how to run; they do not report a run, so they need no companion log."""
    return (path.endswith(".md")
            and not is_support_file(path)
            and not re.search(r"-(correction|analysis|addendum)\.md$", path))


def cited_evidence(text):
    """Raw-artifact filenames a record cites in backticks (.log/.txt/.csv/.ips/.py).

    Multi-condition runs legitimately name their logs `<slug>-baseline.log` and
    `<slug>-enabled.log`, which cannot share a prefix with a record named for its
    verdict (`<slug>-confirmed.md`). The provenance rule therefore also accepts a
    citation, provided that artifact is added in the same PR."""
    return {os.path.basename(m.group(1).strip())
            for m in re.finditer(r"`(?:tests/results/)?([A-Za-z0-9_.\-]+\.(?:log|txt|csv|ips|py))`", text)}


def record_accompanied(record, others, text):
    """True when an added record has raw evidence in the same PR: a companion
    whose name starts with the record's stem, or a cited artifact that was added."""
    stem = os.path.basename(record)[:-3]
    if any(o.startswith(stem) for o in others):
        return True
    return bool(cited_evidence(text) & set(others))


def selftest():
    bad = []
    for path, want in TIER_CASES:
        got = tier_of([path])
        if got != want:
            bad.append(f"{path}: want {want}, got {got}")
    if tier_of([]) != "agent":
        bad.append("empty list: want agent (unknown -> require a verdict)")
    if tier_of(["docs/recomp/PLAN.md", "ui/app.js"]) != "agent":
        bad.append("mixed list: want agent (one agent path makes the PR agent)")
    # fork-log: the agreed routine's three paths
    H = "engine/include/openscanner/obscan_api.h"; F = "docs/recomp/FORK-COORDINATION.md"
    if fork_log_problem([H, "docs/recon/findings/x.md"]) is None:
        bad.append("fork-log: header-only must be a problem")
    if fork_log_problem([H, F]) is not None:
        bad.append("fork-log: header + its log line must pass")
    if fork_log_problem(["ui/app.js"]) is not None:
        bad.append("fork-log: no header must pass")
    # record-provenance: what counts as a run record
    if not is_run_record("tests/results/2026-10-19-x.md"):
        bad.append("record-provenance: a plain record must be a record")
    if is_run_record("tests/results/_TEMPLATE.md"):
        bad.append("record-provenance: _-prefixed support files must not be records")
    if is_run_record("tests/results/x-correction.md"):
        bad.append("record-provenance: corrections must not be records")
    # record-provenance: a stem companion, or a cited artifact added in the PR
    if not record_accompanied("tests/results/x.md", ["x.log"], ""):
        bad.append("record-provenance: a stem companion must satisfy")
    if record_accompanied("tests/results/x-confirmed.md", ["x-baseline.log"], ""):
        bad.append("record-provenance: an unrelated companion alone must not satisfy")
    if not record_accompanied("tests/results/x-confirmed.md", ["x-baseline.log"],
                              "raw log `tests/results/x-baseline.log`"):
        bad.append("record-provenance: a cited added artifact must satisfy")
    if record_accompanied("tests/results/x-confirmed.md", [],
                          "raw log `tests/results/x-baseline.log`"):
        bad.append("record-provenance: a citation of a file not in the PR must not satisfy")
    # build output (row #185): a failing ctest run must carry the FAILED block
    # and the per-test output into the log; a passing or empty run must not.
    sample = ("Test project /x\n"
              "The following tests FAILED:\n"
              "\t  5 - raw_capture_test (Failed)\n"
              "Errors while running CTest\n")
    block = ctest_failure_block(8, sample, "ctest stderr\n")
    if "The following tests FAILED" not in block or "raw_capture_test (Failed)" not in block:
        bad.append("build-output: the FAILED block must survive into the log")
    if "exit 8" not in block or "ctest stderr" not in block:
        bad.append("build-output: the exit code and stderr must be kept")
    if "(no ctest output was captured)" not in ctest_failure_block(8, "", ""):
        bad.append("build-output: an empty capture must say so")
    if not ctest_failed(8, sample) or ctest_failed(0, sample):
        bad.append("build-output: only a non-zero exit is a failure")
    if ctest_failed(8, "No tests were found"):
        bad.append("build-output: an empty suite is not a ctest failure")

    # archives: the record factory's line must verify, and a hand-typed hash
    # must not (row #165). `fetch` stands in for tests/results/<name> at head.
    fixtures = {
        "2026-01-01-hang.raw.log": b"",                      # a hang's 0-byte log
        "2026-01-01-pass.raw.log": b"frame 1\nframe 2\n",
    }
    fetch = lambda name: fixtures.get(name)
    for name, content in fixtures.items():
        sha = sha256_bytes(content)
        line = f"- Raw log: [`{name}`](./{name}), sha256 `{sha}`\n"
        probs, verified = citation_problems(line, fetch, "x.md")
        if probs or verified != 1:
            bad.append(f"archives: the generated line for {name} must verify ({probs})")
        # The same line, one hex digit hand-typed wrong.
        wrong_sha = sha[:-1] + ("0" if sha[-1] != "0" else "1")
        probs, verified = citation_problems(
            f"- Raw log: [`{name}`](./{name}), sha256 `{wrong_sha}`\n", fetch, "x.md")
        if not probs or verified != 0:
            bad.append(f"archives: a hand-typed hash for {name} must fail")
    probs, verified = citation_problems(
        "- Raw log: [`absent.raw.log`](./absent.raw.log), sha256 `" + "a" * 64 + "`\n", fetch, "x.md")
    if not probs or verified != 0:
        bad.append("archives: a citation with no file beside it must fail")

    # The generator itself, through the same resolver: the acceptance
    # sentence end to end. Skipped where the script is not runnable here.
    gen = os.path.join("scripts", "hw-postflight.sh")
    if os.path.isfile(gen):
        with tempfile.TemporaryDirectory() as td:
            for name, content in fixtures.items():
                path = os.path.join(td, name)
                with open(path, "wb") as f:
                    f.write(content)
                r = sh(["bash", gen, "--citation", path])
                if r.returncode != 0:
                    bad.append(f"record-factory: the generator failed for {name}: {r.stderr.strip()}")
                    continue
                print(f"record-factory: {name} -> {r.stdout.strip()}")
                probs, verified = citation_problems(r.stdout, fetch, "x.md")
                if probs or verified != 1:
                    bad.append(f"record-factory: the generated line for {name} must verify ({probs})")
                wrong = re.sub(r"sha256 `([0-9a-f]{64})`",
                               lambda m: "sha256 `" + m.group(1)[:-1] + "f`" if m.group(1)[-1] != "f"
                               else "sha256 `" + m.group(1)[:-1] + "0`", r.stdout)
                probs, verified = citation_problems(wrong, fetch, "x.md")
                if not probs or verified != 0:
                    bad.append(f"record-factory: a hand-typed hash for {name} must fail")
            r = sh(["bash", gen, "--citation", os.path.join(td, "absent.raw.log")])
            if r.returncode == 0:
                bad.append("record-factory: a missing raw log must be refused")
    else:
        print(f"record-factory: SKIP (no {gen} in this tree)")

    for b in bad:
        print("SELFTEST FAIL:", b)
    print(f"selftest: {len(TIER_CASES)} tier cases + record/archives/build-output cases, {'FAIL' if bad else 'ok'}")
    return 1 if bad else 0


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def git(*args):
    return sh(["git"] + list(args))


def git_bytes(*args):
    return subprocess.run(["git"] + list(args), capture_output=True).stdout


def ctest_failed(returncode, output):
    """True when a ctest run is a failure (row #185).

    `No tests were found` is not a failure: the suite is empty at this
    configuration, which the report says separately.
    """
    return returncode != 0 and "No tests were found" not in output


def ctest_failure_block(returncode, stdout, stderr):
    """The block printed to the CI log when ctest fails (row #185).

    `--output-on-failure` already captures the failing tests' output; before
    this change it was discarded and the report carried only the exit code,
    so a CI failure could not name a test. Keep the whole capture: ctest puts
    `The following tests FAILED` and the per-test output in stdout, and some
    platforms add diagnostics on stderr.
    """
    captured = (stdout + stderr).strip()
    return "\n".join([
        f"--- ctest failed (exit {returncode}) ---",
        captured if captured else "(no ctest output was captured)",
        "--- end ctest output ---",
    ])


# The whitespace rule, in one place: `scripts/git-hooks/pre-commit` runs this
# too, via `verify-pr.py --staged`, so the hook and CI cannot disagree.
# Verbatim run captures are exempt — a raw log is the device's own output
# (trailing spaces and all) and must never be edited to satisfy a linter.
WHITESPACE_EXCLUDES = (":(exclude)tests/results/*.log", ":(exclude)tests/results/*.ips")


def whitespace_check(range_args):
    """`git diff --check` with the repo's exclusions.

    `range_args` is e.g. `["origin/main...HEAD"]` for a PR or `["--cached"]`
    for the staged set. Returns (ok, raw report).
    """
    r = git("diff", "--check", *range_args, "--", ".", *WHITESPACE_EXCLUDES)
    out = (r.stdout + r.stderr).strip()
    return (not out), out


def staged_whitespace():
    """Pre-commit entry point: refuse a commit whose staged content is dirty."""
    ok, out = whitespace_check(["--cached"])
    if ok:
        return 0
    print("pre-commit: whitespace errors in the staged content — refusing the commit:", file=sys.stderr)
    print(out, file=sys.stderr)
    print("Fix the lines above and re-stage. `git commit --no-verify` bypasses this check.", file=sys.stderr)
    return 1


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def citation_problems(text, fetch, label):
    """Resolve the `sha256 `<hex>`` claims a record makes (the `archives` rule).

    `fetch(name)` returns the bytes of `tests/results/<name>` at the PR head,
    or None when that file is not there. The cited artifact is the nearest
    name in the 300 characters before the hash — a link target
    `(./x.raw.log)` or a backticked `x.raw.log` — so the generated line

        - Raw log: [`x.raw.log`](./x.raw.log), sha256 `<hex>`

    resolves to tests/results/x.raw.log. The first name that resolves is the
    one compared; a name that is not in the tree does not resolve at all.

    Returns (problems, verified_count). Row #165 pins this function with the
    selftest cases below: the record factory's generated line must verify and
    a wrong hand-typed hash must not.
    """
    problems, verified = [], 0
    for m in re.finditer(r"sha256\s*`([0-9a-f]{64})`", text):
        cited = m.group(1)
        ctx = text[max(0, m.start() - 300):m.start()]
        names = [a or b for a, b in re.findall(r"\(\./([^)]+)\)|`([^`]+\.(?:ips|md|log|txt))`", ctx)]
        resolved = False
        for name in names[::-1]:
            data = fetch(name)
            if data is None:
                continue
            resolved = True
            if sha256_bytes(data) == cited:
                verified += 1
            else:
                problems.append(f"{label}: cited sha256 does not match tests/results/{name}")
            break
        if not resolved:
            problems.append(
                f"{label}: cited sha256 {cited[:12]}… does not resolve to a file named next to it")
    return problems, verified


def pr_files(pr):
    r = sh(["gh", "api", f"repos/{REPO}/pulls/{pr}/files", "--paginate", "--jq", ".[].filename"])
    if r.returncode != 0:
        return None
    return [l for l in r.stdout.splitlines() if l]


def tier_of(paths):
    if not paths:
        return "agent"  # unknown -> require a verdict
    return "agent" if any(is_agent_tier(p) for p in paths) else "script"


def probe_matrix():
    cases = []
    for cwd_name, cwd in [("wt-hw", WT_HW), ("main", MAIN), ("wt-verify", WT_VERIFY), ("tmp", "/tmp")]:
        for cmd_name, cmd in [
            ("device-test", DEVICE_TEST),
            ("data-process-test", DATA_TEST),
            ("cd-hw+device", f"cd {WT_HW} && {DEVICE_TEST}"),
            ("dyld", "DYLD_INSERT_LIBRARIES=/x ./run"),
            ("recon-cache", "cat .recon-cache/listings/x.asm"),
            ("header", "cat engine/include/openscanner/obscan_api.h"),
            ("vendor-sdk", "strings libOrbbecSDK.1.7.1.dylib"),
            ("vendor-scan", "cat liborbbec_scan.3.5.3.dylib"),
            ("ls", "ls -la"),
        ]:
            cases.append((f"{cwd_name}/{cmd_name}", cwd, cmd))
    return cases


def run_hook(script_bytes, cwd, cmd):
    with tempfile.TemporaryDirectory() as td:
        sp = os.path.join(td, "hook.sh")
        with open(sp, "wb") as f:
            f.write(script_bytes)
        os.chmod(sp, 0o755)
        payload = json.dumps({"cwd": cwd, "tool_name": "Bash", "tool_input": {"command": cmd}})
        r = subprocess.run(["bash", sp], input=payload, capture_output=True, text=True)
        return r.returncode


def run_probes(script_bytes, matrix):
    return {label: ("allow" if run_hook(script_bytes, cwd, cmd) == 0 else "block")
            for label, cwd, cmd in matrix}


HOOK_MUST_BLOCK = [
    ("dyld/tmp", "/tmp", "DYLD_INSERT_LIBRARIES=/x ./run"),
    ("vendor-sdk/tmp", "/tmp", "strings libOrbbecSDK.1.7.1.dylib"),
    ("vendor-scan/tmp", "/tmp", "cat liborbbec_scan.3.5.3.dylib"),
    ("recon-cache/tmp", "/tmp", "cat .recon-cache/listings/x.asm"),
    ("header/tmp", "/tmp", "cat engine/include/openscanner/obscan_api.h"),
    ("device/main", MAIN, DEVICE_TEST),
    ("device/wt-verify", WT_VERIFY, DEVICE_TEST),
    ("device/tmp", "/tmp", DEVICE_TEST),
    ("data/main", MAIN, DATA_TEST),
    ("data/wt-verify", WT_VERIFY, DATA_TEST),
    ("data/tmp", "/tmp", DATA_TEST),
]
HOOK_MUST_ALLOW = [
    ("device/wt-hw", WT_HW, DEVICE_TEST),
    ("data/wt-hw", WT_HW, DATA_TEST),
    ("device/cd-wt-hw", "/tmp", f"cd {WT_HW} && {DEVICE_TEST}"),
    ("ls/tmp", "/tmp", "ls -la"),
]


def check_hook_invariants(script_bytes):
    bad = []
    for label, cwd, cmd in HOOK_MUST_BLOCK:
        if run_hook(script_bytes, cwd, cmd) == 0:
            bad.append(f"must-block case allowed: {label}")
    for label, cwd, cmd in HOOK_MUST_ALLOW:
        if run_hook(script_bytes, cwd, cmd) != 0:
            bad.append(f"must-allow case blocked: {label}")
    return bad


def status_of(line):
    cells = line.split("|")
    return cells[3].strip() if len(cells) > 4 else "?"


def main():
    argv = sys.argv[1:]
    # Pre-commit mode: the staged set, no PR/network. Kept first so the hook is
    # independent of gh and of any PR state.
    if "--staged" in argv:
        return staged_whitespace()
    build_flag = "--build" in argv
    no_build = "--no-build" in argv
    tier_mode = "--tier" in argv
    self_test = "--selftest" in argv
    if self_test:
        return selftest()

    nums = [a for a in argv if a.isdigit()]
    if not nums:
        print(__doc__)
        return 2
    pr = nums[0]

    if tier_mode:
        files = pr_files(pr)
        print(tier_of(files))
        return 0

    r = sh(["gh", "pr", "view", pr, "--repo", REPO,
            "--json", "number,state,baseRefName,baseRefOid,headRefOid,body,url,mergeCommit"])
    if r.returncode != 0:
        print(f"ENV: gh pr view {pr} failed: {r.stderr.strip()}")
        return 2
    meta = json.loads(r.stdout)
    state = meta["state"]
    head_sha = meta["headRefOid"]
    base_name = meta["baseRefName"]
    body = meta["body"] or ""
    changed = pr_files(pr) or []
    tier = tier_of(changed)

    if git("fetch", "-q", "origin", base_name).returncode != 0:
        print("ENV: git fetch of base branch failed")
        return 2
    head_ref = "HEAD"
    if git("rev-parse", "HEAD").stdout.strip() != head_sha:
        # --force: an earlier read may have created this ref, and a rebased or
        # amended head makes the update non-fast-forward (which used to fail
        # as a misleading "ENV: fetch of refs/pull/... failed").
        if git("fetch", "-q", "--force", "origin", f"pull/{pr}/head:refs/verify/pr-{pr}").returncode != 0:
            print(f"ENV: fetch of refs/pull/{pr}/head failed")
            return 2
        head_ref = f"refs/verify/pr-{pr}"
    # For a merged PR, diff against the merge commit's first parent; the head
    # is already an ancestor of the base branch, so `origin/<base>...head`
    # would be empty.
    if state == "MERGED" and (meta.get("mergeCommit") or {}).get("oid"):
        base_ref = f"{meta['mergeCommit']['oid']}^1"
    else:
        base_ref = f"origin/{base_name}"
    if git("rev-parse", "--verify", "-q", base_ref).returncode != 0:
        print(f"ENV: base ref {base_ref} does not resolve")
        return 2
    got = git("rev-parse", head_ref).stdout.strip()
    if got != head_sha:
        print(f"ENV: fetched head {got[:12]} != PR head {head_sha[:12]}")
        return 2

    report = []
    fails = []

    def add(name, ok, detail):
        report.append((name, "PASS" if ok else "FAIL", detail))
        if not ok:
            fails.append(name)

    # 1. whitespace — the shared rule (see whitespace_check; the pre-commit
    #    hook runs the same code on the staged set).
    ws_ok, ws_out = whitespace_check([f"{base_ref}...{head_ref}"])
    add("whitespace", ws_ok, ws_out[:400] or "clean")

    # 2. run records never edited, deleted or renamed in place
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    bad = []
    exempted = 0
    for line in r.stdout.splitlines():
        st, path = line.split("\t")[0], line.split("\t")[-1]
        bn = os.path.basename(path)
        is_addendum = (bn.endswith("-correction.md") or bn.endswith("-analysis.md")
                       or bn.endswith("-addendum.md"))
        if st.startswith("M"):
            if is_addendum or is_support_file(path):
                exempted += 1
            else:
                bad.append(f"modified in place: {path}")
        elif st.startswith(("D", "R")):
            bad.append(f"deleted/renamed: {path}")
    add("run-records", not bad,
        "; ".join(bad) if bad else
        (f"{exempted} modified addendum/support file(s) allowed" if exempted else "no modifications"))

    # 2b. added records need a companion raw artifact in the same PR (#21)
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    added_res = [l.split("\t")[-1] for l in r.stdout.splitlines() if l.startswith("A")]
    records = [p for p in added_res if is_run_record(p)]
    others = [os.path.basename(p) for p in added_res
              if p not in records and not p.endswith(".md")]
    missing = []
    for p in records:
        stem_ok = any(o.startswith(os.path.basename(p)[:-3]) for o in others)
        text = "" if stem_ok else git("show", f"{head_ref}:{p}").stdout
        if not record_accompanied(p, others, text):
            missing.append(p)
    add("record-provenance", not missing,
        "; ".join(f"no raw evidence in this PR (companion or cited artifact): {p}" for p in missing)
        if missing
        else (f"{len(records)} record(s) with evidence" if records
              else "no records added"))

    # 3. plan: syntax gated; status moves and new rows reported only
    r = git("diff", "--name-only", f"{base_ref}...{head_ref}", "--", "docs/recomp/PLAN.md")
    if r.stdout.strip():
        head_plan = git("show", f"{head_ref}:docs/recomp/PLAN.md").stdout
        base_plan = git("show", f"{base_ref}:docs/recomp/PLAN.md").stdout
        hrows, brows = {}, {}
        for line in head_plan.splitlines():
            m = re.match(r"\|\s*(\d+)\s*\|", line)
            if m:
                hrows[int(m.group(1))] = line
        for line in base_plan.splitlines():
            m = re.match(r"\|\s*(\d+)\s*\|", line)
            if m:
                brows[int(m.group(1))] = line
        probs = []
        nums_seen = [m.group(1) for m in re.finditer(r"^\|\s*(\d+)\s*\|", head_plan, re.M)]
        dups = sorted({n for n in nums_seen if nums_seen.count(n) > 1})
        if dups:
            probs.append("duplicate row numbers: " + ", ".join(dups))
        for n, line in sorted(hrows.items()):
            cells = line.split("|")
            if len(cells) < 6:
                probs.append(f"row {n}: malformed")
            elif cells[3].strip() not in STATUSES:
                probs.append(f"row {n}: bad status {cells[3].strip()!r}")
        moved = [f"#{n}: {status_of(brows[n])} -> {status_of(hrows[n])}"
                 for n in sorted(hrows) if n in brows and status_of(brows[n]) != status_of(hrows[n])]
        added = [f"#{n}" for n in sorted(hrows) if n not in brows]
        detail = "; ".join(probs) if probs else \
            (("moves (reported): " + ", ".join(moved)) if moved else "no status moves") + \
            (f"; new rows: {', '.join(added)}" if added else "")
        add("plan", not probs, detail)
    else:
        report.append(("plan", "INFO", "PLAN.md unchanged"))

    # 4. archival claims
    diff_files = git("diff", "--name-status", f"{base_ref}...{head_ref}").stdout
    added = [l.split("\t")[-1] for l in diff_files.splitlines() if l.startswith("A")]
    md_touched = [l.split("\t")[-1] for l in diff_files.splitlines()
                  if re.search(r"tests/results/.*\.md$", l.split("\t")[-1])]
    arch_problems, arch_verified, arch_local = [], 0, 0
    for path in added:
        if not path.endswith(".ips"):
            continue
        bn = os.path.basename(path)
        src = os.path.expanduser(f"~/Library/Logs/DiagnosticReports/{bn}")
        if os.path.exists(src):
            arch_local += 1
            if sha256_bytes(open(src, "rb").read()) != sha256_bytes(git_bytes("show", f"{head_ref}:{path}")):
                arch_problems.append(f"{bn}: sha256 mismatch vs {src}")
    for path in md_touched:
        text = git("show", f"{head_ref}:{path}").stdout

        def fetch(name, _ref=head_ref):
            p = f"tests/results/{name}"
            if git("cat-file", "-e", f"{_ref}:{p}").returncode != 0:
                return None
            return git_bytes("show", f"{_ref}:{p}")

        probs, verified = citation_problems(text, fetch, path)
        arch_problems += probs
        arch_verified += verified
    arch_detail = "; ".join(filter(None, [
        f"{arch_verified} cited hash(es) verified" if arch_verified else "",
        f"{arch_local} .ips compared against DiagnosticReports (local)" if arch_local else "",
    ])) or "no archival claims"
    add("archives", not arch_problems,
        "; ".join(arch_problems) if arch_problems else arch_detail)

    # 5. hook executable + invariants + matrix diff
    hook = ".claude/hooks/guard-hw-run.sh"
    if hook in [l.split("\t")[-1] for l in diff_files.splitlines()]:
        mode = git("ls-tree", head_ref, "--", hook).stdout.split()[0]
        head_hook = git_bytes("show", f"{head_ref}:{hook}")
        base_hook = git_bytes("show", f"{base_ref}:{hook}")
        problems = [] if mode == "100755" else [f"mode {mode} (want 100755)"]
        problems += check_hook_invariants(head_hook)
        matrix = probe_matrix()
        rb, rh = run_probes(base_hook, matrix), run_probes(head_hook, matrix)
        diffs = [f"{label}: {rb[label]} -> {rh[label]}" for label in rb if rb[label] != rh[label]]
        add("hook", not problems, "; ".join(problems) if problems else
            "executable (100755); guard invariants hold")
        report.append(("hook-probes", "INFO",
                       f"{len(matrix)} cases; diffs: " + ("; ".join(diffs) if diffs else "none")))
    else:
        report.append(("hook", "INFO", "unchanged"))

    # 6. header classification
    hdr = "engine/include/openscanner/obscan_api.h"
    if hdr in [l.split("\t")[-1] for l in diff_files.splitlines()]:
        def strip_cpp(t):
            t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
            t = re.sub(r"//[^\n]*", "", t)
            return re.sub(r"\s+", " ", t).strip()
        b = strip_cpp(git("show", f"{base_ref}:{hdr}").stdout)
        h = strip_cpp(git("show", f"{head_ref}:{hdr}").stdout)
        report.append(("header", "INFO",
                       "comment-only change" if b == h else "code text changed — semantic review needed"))
    else:
        report.append(("header", "INFO", "unchanged"))

    # 6b. UI JavaScript parse check — the #54 class: a duplicate binding in
    # ui/app.js killed the whole page, and nothing in this gate parsed the
    # page's scripts. Changed ui/*.js must parse (node; module-or-script per
    # the file's own import/export usage).
    js_files = [p for p in changed if re.match(r"^ui/.*\.js$", p)]
    if not js_files:
        report.append(("ui-js-syntax", "INFO", "no ui JS changed"))
    elif sh(["which", "node"]).returncode != 0:
        report.append(("ui-js-syntax", "INFO", "node not found on PATH — skipped (CI covers)"))
    else:
        js_bad, js_checked = [], 0
        for p in js_files:
            if git("cat-file", "-e", f"{head_ref}:{p}").returncode != 0:
                continue  # deleted in this PR
            content = git_bytes("show", f"{head_ref}:{p}")
            esm = bool(re.search(rb"(?m)^\s*(import|export)\b", content))
            tmp = os.path.join(tempfile.gettempdir(),
                               f"verify-pr-{pr}-{os.path.basename(p)}{'.mjs' if esm else '.js'}")
            with open(tmp, "wb") as fh:
                fh.write(content)
            r = sh(["node", "--check", tmp])
            js_checked += 1
            if r.returncode != 0:
                first = (r.stderr.strip().splitlines() or ["parse failed"])[0]
                js_bad.append(f"{p}: {first[:160]}")
        add("ui-js-syntax", not js_bad,
            "; ".join(js_bad) if js_bad else f"{js_checked} file(s) parse")

    # 6b. fork-log: the agreed routine — a PR that moves the contract header carries its §5
    # line in the coordination file, in the same PR (the fork's --pincheck is the drift alarm).
    changed = [l.split("\t")[-1] for l in diff_files.splitlines()]
    fl = fork_log_problem(changed)
    add("fork-log", fl is None,
        fl if fl else ("header unchanged" if "engine/include/openscanner/obscan_api.h" not in changed
                       else "header + its fork-log line together"))

    # 7. inventory vs PR body
    body_l = body.lower()

    def mentioned(p):
        # Full path, basename, or the immediate parent directory name (so
        # "tests/hardware/<name>/main.cpp" counts when the body names <name>).
        # Deliberately no broader segment matching: it masked unclaimed files.
        if p in body or os.path.basename(p) in body:
            return True
        if p == "docs/recomp/PLAN.md":
            return True  # the board is routinely touched without being named
        parent = os.path.dirname(p).split("/")[-1]
        if len(parent) >= 5 and parent.lower() in body_l:
            return True
        return False

    unmentioned = [p for p in changed if not mentioned(p)]
    report.append(("inventory", "INFO",
                   f"{len(changed)} file(s); not mentioned in PR body: "
                   + (", ".join(unmentioned[:8]) + ("…" if len(unmentioned) > 8 else "")
                      if unmentioned else "none")))

    # 8. mode changes
    r = git("diff", "--summary", f"{base_ref}...{head_ref}")
    modes = [l for l in r.stdout.splitlines() if "mode change" in l]
    report.append(("modes", "INFO", "; ".join(modes) if modes else "no mode changes"))

    # 9. build + ctest
    at_head = git("rev-parse", "HEAD").stdout.strip() == head_sha
    if no_build:
        report.append(("build", "INFO", "skipped (--no-build)"))
    elif at_head or build_flag:
        rc = sh(["cmake", "-S", ".", "-B", "build"]).returncode
        if rc == 0:
            rc = sh(["cmake", "--build", "build", "-j"]).returncode
        if rc != 0:
            add("build", False, "cmake configure/build failed (see CI log)")
        else:
            ct = sh(["ctest", "--test-dir", "build", "-LE", "hardware", "--output-on-failure"])
            output = ct.stdout + ct.stderr
            no_tests = "No tests were found" in output
            # Row #185: the CI log must name the failing tests. The decision
            # is unchanged (the exit code, with the empty-suite case allowed);
            # only the captured output is now carried into the log.
            if ctest_failed(ct.returncode, output):
                print()
                print(ctest_failure_block(ct.returncode, ct.stdout, ct.stderr))
                print()
                sys.stdout.flush()
            add("build", not ctest_failed(ct.returncode, output),
                "build ok; ctest: " + ("no tests selected" if no_tests else f"exit {ct.returncode}"))
    else:
        report.append(("build", "INFO", "skipped (working tree is not the PR head; runs in CI)"))

    # --- report
    short = head_sha[:12]
    print(f"# verify-pr — PR #{pr} (head `{short}`, base `{base_name}`, tier **{tier}**)")
    print()
    print("| check | result | detail |")
    print("|---|---|---|")
    for name, st, detail in report:
        print(f"| {name} | {st} | {detail.replace('|', '/')} |")
    print()
    print("Gating checks: whitespace, run-records, record-provenance, plan, archives, hook, ui-js-syntax, fork-log, build.")
    if fails:
        print()
        print(f"**FAILED: {', '.join(fails)}**")
        return 1
    print()
    print("All gating checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
