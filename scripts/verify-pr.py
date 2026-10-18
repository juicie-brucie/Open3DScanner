#!/usr/bin/env python3
"""Deterministic pre-verification checks for an OpenScanner PR.

Usage:
    scripts/verify-pr.py <pr-number> [--build] [--no-build]
    scripts/verify-pr.py --tier <pr-number>      # print: script | agent

Works on git refs (no checkout required): fetches `refs/pull/<n>/head` and the
PR's base branch, then checks the diff. Exit 0 = all gating checks passed;
1 = a gating check failed; 2 = environment problem.

Tiers (printed with the report; used by verdict-gate.yml to decide whether a PR
is script-gated or needs an agent verdict):
    agent tier   engine/**, tools/**, mesh/**, ui/**, scripts/**, tests/hardware/**,
                 any CMakeLists.txt, .github/**, .claude/**, docs/recon/**,
                 tests/results/** addenda (*-correction.md, *-analysis.md)
    script tier  everything else (run records, raw logs, .ips, PLAN.md,
                 docs/recomp/**, CLAUDE.md, README.md, ...)
    An empty or unreadable file list is treated as agent tier.

Gating checks
  whitespace      `git diff --check`
  run-records     tests/results/: no existing record modified, deleted or renamed
                  in place (addenda *-correction.md / *-analysis.md excepted)
  plan            PLAN.md row syntax: unique numbers, valid statuses, well-formed.
                  Status moves and new rows are reported, not gated.
  archives        cited `sha256 <hex>` values in changed tests/results/*.md must
                  match a file named next to the citation in the PR; a citation
                  that cannot be resolved fails. Added *.ips files are compared
                  against ~/Library/Logs/DiagnosticReports/<same basename> when
                  that directory exists (local best-effort; CI has no such dir).
  hook            .claude/hooks/guard-hw-run.sh is executable, and the head hook
                  satisfies the guard invariants (must-block / must-allow cases
                  below). A base-vs-head probe matrix diff is reported.
  build           cmake configure + build + ctest -LE hardware; runs only when
                  the working tree is the PR head or --build is passed (CI runs
                  it from the head checkout).

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
    r"^docs/recon/",
    r"^tests/results/.*(-correction|analysis)\.md$",
]


def is_agent_tier(path):
    return any(re.search(p, path) for p in AGENT_PATTERNS)


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def git(*args):
    return sh(["git"] + list(args))


def git_bytes(*args):
    return subprocess.run(["git"] + list(args), capture_output=True).stdout


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


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
    build_flag = "--build" in argv
    no_build = "--no-build" in argv
    tier_mode = "--tier" in argv
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
        if git("fetch", "-q", "origin", f"pull/{pr}/head:refs/verify/pr-{pr}").returncode != 0:
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

    # 1. whitespace
    r = git("diff", "--check", f"{base_ref}...{head_ref}")
    add("whitespace", not (r.stdout.strip() or r.stderr.strip()),
        (r.stdout + r.stderr).strip()[:400] or "clean")

    # 2. run records never edited, deleted or renamed in place
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    bad = []
    addenda = 0
    for line in r.stdout.splitlines():
        st, path = line.split("\t")[0], line.split("\t")[-1]
        bn = os.path.basename(path)
        is_addendum = bn.endswith("-correction.md") or bn.endswith("-analysis.md")
        if st.startswith("M"):
            if is_addendum:
                addenda += 1
            else:
                bad.append(f"modified in place: {path}")
        elif st.startswith(("D", "R")):
            bad.append(f"deleted/renamed: {path}")
    add("run-records", not bad,
        "; ".join(bad) if bad else
        (f"{addenda} modified addenda allowed" if addenda else "no modifications"))

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
        for m in re.finditer(r"sha256\s*`([0-9a-f]{64})`", text):
            cited = m.group(1)
            ctx = text[max(0, m.start() - 300):m.start()]
            names = [a or b for a, b in re.findall(r"\(\./([^)]+)\)|`([^`]+\.(?:ips|md|log|txt))`", ctx)]
            resolved = False
            for name in names[::-1]:
                p = f"tests/results/{name}"
                if git("cat-file", "-e", f"{head_ref}:{p}").returncode == 0:
                    if sha256_bytes(git_bytes("show", f"{head_ref}:{p}")) == cited:
                        arch_verified += 1
                        resolved = True
                    else:
                        arch_problems.append(f"{path}: cited sha256 does not match {p}")
                        resolved = True
                    break
            if not resolved:
                arch_problems.append(
                    f"{path}: cited sha256 {cited[:12]}… does not resolve to a file named next to it")
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
            no_tests = "No tests were found" in (ct.stdout + ct.stderr)
            add("build", ct.returncode == 0 or no_tests,
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
    print("Gating checks: whitespace, run-records, plan, archives, hook, build.")
    if fails:
        print()
        print(f"**FAILED: {', '.join(fails)}**")
        return 1
    print()
    print("All gating checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
