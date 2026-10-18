#!/usr/bin/env python3
"""Deterministic pre-verification checks for an OpenScanner PR.

Usage:
    scripts/verify-pr.py <pr-number> [--build] [--no-build]

Works on git refs (no checkout required): fetches `refs/pull/<n>/head` and the
PR's base branch, then checks the diff. Exit 0 = all gating checks passed;
1 = a gating check failed; 2 = environment problem.

Gating checks: whitespace, run-records (no existing run record modified in
place), plan (PLAN.md row syntax + status moves), archives (added *.ips match
their ~/Library/Logs/DiagnosticReports source, when present), hashes (sha256
citations in tests/results/*.md match the file they name), hook (executable),
build (cmake + ctest; runs only when the working tree is the PR head or --build).

Reported only: inventory (files not mentioned in the PR body), header
(comment-only vs code change in obscan_api.h), modes, hook-probes (per-case
allow/block matrix diff between base and head).
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


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def git(*args):
    return sh(["git"] + list(args))


def git_bytes(*args):
    return subprocess.run(["git"] + list(args), capture_output=True).stdout


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def build_probe_matrix():
    wt_hw = "/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw"
    main = "/Users/brucemackay/DevReposiMac/Open3DScanner"
    wt_verify = "/Users/brucemackay/DevReposiMac/Open3DScanner-wt-verify"
    d = "./build/tests/hardware/device_message_listener_test/device_message_listener_test"
    p = "./build/tests/hardware/data_process_message_listener_test/data_process_message_listener_test"
    cases = []
    for cwd_name, cwd in [("wt-hw", wt_hw), ("main", main), ("wt-verify", wt_verify), ("tmp", "/tmp")]:
        for cmd_name, cmd in [
            ("device-test", d),
            ("data-process-test", p),
            ("cd-hw+device", f"cd {wt_hw} && {d}"),
            ("dyld", "DYLD_INSERT_LIBRARIES=/x ./run"),
            ("recon-cache", "cat .recon-cache/x"),
            ("header", "cat engine/include/openscanner/obscan_api.h"),
            ("ls", "ls -la"),
        ]:
            cases.append((f"{cwd_name}/{cmd_name}", cwd, cmd))
    return cases


def run_probes(script_bytes, matrix):
    out = {}
    with tempfile.TemporaryDirectory() as td:
        sp = os.path.join(td, "hook.sh")
        with open(sp, "wb") as f:
            f.write(script_bytes)
        os.chmod(sp, 0o755)
        for label, cwd, cmd in matrix:
            payload = json.dumps({"cwd": cwd, "tool_name": "Bash", "tool_input": {"command": cmd}})
            r = subprocess.run(["bash", sp], input=payload, capture_output=True, text=True)
            out[label] = "allow" if r.returncode == 0 else f"block({r.returncode})"
    return out


def status_of(line):
    cells = line.split("|")
    return cells[3].strip() if len(cells) > 4 else "?"


def main():
    argv = sys.argv[1:]
    build_flag = "--build" in argv
    no_build = "--no-build" in argv
    nums = [a for a in argv if a.isdigit()]
    if not nums:
        print(__doc__)
        return 2
    pr = nums[0]

    r = sh(["gh", "pr", "view", pr, "--repo", REPO,
            "--json", "number,state,baseRefName,baseRefOid,headRefOid,body,url,files,mergeCommit"])
    if r.returncode != 0:
        print(f"ENV: gh pr view {pr} failed: {r.stderr.strip()}")
        return 2
    meta = json.loads(r.stdout)
    state = meta["state"]
    head_sha = meta["headRefOid"]
    base_name = meta["baseRefName"]
    body = meta["body"] or ""
    changed = [f["path"] for f in meta.get("files", [])]

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

    # 2. run records never edited in place (addenda may be amended)
    r = git("diff", "--name-status", f"{base_ref}...{head_ref}", "--", "tests/results/")
    mods = [l for l in r.stdout.splitlines() if l.startswith("M")]
    bad = []
    for l in mods:
        path = l.split("\t")[-1]
        if re.search(r"correction|analysis", os.path.basename(path)):
            continue
        bad.append(path)
    add("run-records", not bad,
        ("modified in place: " + ", ".join(bad)) if bad else
        (f"{len(mods)} modified addenda allowed" if mods else "no modifications"))

    # 3. plan syntax + status moves
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
            ("moves: " + ", ".join(moved) if moved else "no status moves") + \
            (f"; new rows: {', '.join(added)}" if added else "")
        add("plan", not probs, detail)
    else:
        report.append(("plan", "INFO", "PLAN.md unchanged"))

    # 4+5. archival hashes
    diff_files = git("diff", "--name-status", f"{base_ref}...{head_ref}").stdout
    added = [l.split("\t")[-1] for l in diff_files.splitlines() if l.startswith("A")]
    md_touched = [l.split("\t")[-1] for l in diff_files.splitlines()
                  if re.search(r"tests/results/.*\.md$", l.split("\t")[-1])]
    arch_problems, arch_checked = [], 0
    for path in added:
        if not path.endswith(".ips"):
            continue
        bn = os.path.basename(path)
        src = os.path.expanduser(f"~/Library/Logs/DiagnosticReports/{bn}")
        if os.path.exists(src):
            arch_checked += 1
            a = sha256_bytes(open(src, "rb").read())
            b = sha256_bytes(git_bytes("show", f"{head_ref}:{path}"))
            if a != b:
                arch_problems.append(f"{bn}: sha256 mismatch vs {src}")
    for path in md_touched:
        text = git("show", f"{head_ref}:{path}").stdout
        for m in re.finditer(r"sha256\s*`([0-9a-f]{64})`", text):
            cited = m.group(1)
            ctx = text[max(0, m.start() - 300):m.start()]
            names = [a or b for a, b in re.findall(r"\(\./([^)]+)\)|`([^`]+\.(?:ips|md|log|txt))`", ctx)]
            matched = None
            for name in names[::-1]:
                p = f"tests/results/{name}"
                if git("cat-file", "-e", f"{head_ref}:{p}").returncode == 0:
                    if sha256_bytes(git_bytes("show", f"{head_ref}:{p}")) == cited:
                        matched = p
                    else:
                        arch_problems.append(f"{path}: cited sha256 for {p} does not match")
                    break
            if matched:
                arch_checked += 1
    add("archives", not arch_problems,
        "; ".join(arch_problems) if arch_problems else
        (f"{arch_checked} archival claim(s) verified" if arch_checked else "no archival claims"))

    # 6. hook executable + probe diff
    hook = ".claude/hooks/guard-hw-run.sh"
    if hook in [l.split("\t")[-1] for l in diff_files.splitlines()]:
        mode = git("ls-tree", head_ref, "--", hook).stdout.split()[0]
        problems = [] if mode == "100755" else [f"mode {mode} (want 100755)"]
        base_hook = git_bytes("show", f"{base_ref}:{hook}")
        head_hook = git_bytes("show", f"{head_ref}:{hook}")
        matrix = build_probe_matrix()
        rb = run_probes(base_hook, matrix)
        rh = run_probes(head_hook, matrix)
        diffs = [f"{label}: {rb[label]} -> {rh[label]}" for label in rb if rb[label] != rh[label]]
        add("hook", not problems, "; ".join(problems) if problems else "executable (100755)")
        report.append(("hook-probes", "INFO",
                       f"{len(matrix)} cases; diffs: " + ("; ".join(diffs) if diffs else "none")))
    else:
        report.append(("hook", "INFO", "unchanged"))

    # 7. header classification
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

    # 8. inventory vs PR body
    body_l = body.lower()

    def mentioned(p):
        if p in body or os.path.basename(p) in body:
            return True
        if p == "docs/recomp/PLAN.md":
            return True
        for seg in p.split("/"):
            seg = re.sub(r"\.[a-z0-9]+$", "", seg)
            if len(seg) >= 5 and seg.lower() in body_l:
                return True
        return False

    unmentioned = [p for p in changed if not mentioned(p)]
    report.append(("inventory", "INFO",
                   f"{len(changed)} file(s); not mentioned in PR body: "
                   + (", ".join(unmentioned[:8]) + ("…" if len(unmentioned) > 8 else "")
                      if unmentioned else "none")))

    # 9. mode changes
    r = git("diff", "--summary", f"{base_ref}...{head_ref}")
    modes = [l for l in r.stdout.splitlines() if "mode change" in l]
    report.append(("modes", "INFO", "; ".join(modes) if modes else "no mode changes"))

    # 10. build + ctest
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
    print(f"# verify-pr — PR #{pr} (head `{short}`, base `{base_name}`)")
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
