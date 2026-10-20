#!/usr/bin/env python3
"""pr-rebase.py — the dispatcher's PR rebase pass (intake's one write).

A PR is eligible only when ALL of these hold:
  * it is open and not a draft;
  * its head is not checked out in any worktree;
  * no live session owns the head's slot;
  * it is behind origin/main;
  * no VERDICT comment names its current head (a verdict is the gate; a
    rebase would void it).

The rebase runs through scripts/safe-rebase.sh in a temporary detached
worktree, so the dispatch worktree is never touched.  A conflict aborts and
restores the branch; nothing is pushed.  A clean rebase is published with
--force-with-lease.

Usage:
    python3 scripts/pr-rebase.py --sweep --dry-run
    python3 scripts/pr-rebase.py --sweep --apply

Output: three lists — rebased (old -> new), skipped (reason), conflicted.
Exit: 0 ok; 1 refused (wt-roster check is DRIFT); 2 bad arguments.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SAFE_REBASE = os.path.join(HERE, "safe-rebase.sh")
ROSTER = os.path.join(HERE, "wt-roster.py")
VERDICT_RE = re.compile(r"VERDICT:\s*(MERGEABLE|NEEDS CHANGE)\s*@\s*([0-9a-f]{40})")


def run(cmd, cwd=None, check=False):
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)}: {proc.stderr.strip()}")
    return proc


def git(repo, *args, check=False):
    return run(["git", "-C", repo, *args], check=check)


def repo_root() -> str:
    out = run(["git", "rev-parse", "--show-toplevel"]).stdout.strip()
    if not out:
        sys.exit("pr-rebase: not inside a git worktree")
    return out


def roster_ok(repo: str) -> bool:
    if not os.path.exists(ROSTER):
        print("pr-rebase: wt-roster.py is missing; refusing to apply a rebase sweep")
        return False
    proc = run([sys.executable, ROSTER, "check"], cwd=repo)
    sys.stdout.write(proc.stdout)
    if proc.returncode == 1:
        print("pr-rebase: REFUSING — the roster check is DRIFT")
        return False
    if proc.returncode == 2:
        print("pr-rebase: REFUSING — the roster check is UNVERIFIED")
        return False
    return True


def worktrees(repo: str):
    out = git(repo, "worktree", "list", "--porcelain", check=True).stdout
    items = []
    for block in out.split("\n\n"):
        if not block.strip():
            continue
        item = {"path": "", "branch": None}
        for line in block.splitlines():
            if line.startswith("worktree "):
                item["path"] = line[len("worktree "):]
            elif line.startswith("branch refs/heads/"):
                item["branch"] = line[len("branch refs/heads/"):]
        items.append(item)
    return items


def running_dirs(repo: str) -> set[str]:
    """Directories with a currently running session, if the server answers."""
    try:
        import urllib.request
        import base64
        port = None
        for line in run(["lsof", "-nP", "-iTCP", "-sTCP:LISTEN"]).stdout.splitlines():
            if "opencode" in line:
                m = re.search(r":(\d+)\b", line)
                if m:
                    port = int(m.group(1))
                    break
        if not port:
            return set()
        service = os.environ.get("OPENSCANNER_SERVICE", os.path.expanduser("~/.config/opencode/service.json"))
        password = json.load(open(service, encoding="utf-8")).get("password", "")
        token = base64.b64encode(f"opencode:{password}".encode()).decode()
        req = urllib.request.Request(f"http://127.0.0.1:{port}/api/session/active",
                                     headers={"Authorization": f"Basic {token}"})
        with urllib.request.urlopen(req, timeout=10) as resp:
            active = json.loads(resp.read().decode()).get("data") or {}
        dirs = set()
        for sid in active:
            req = urllib.request.Request(f"http://127.0.0.1:{port}/api/session/{sid}",
                                         headers={"Authorization": f"Basic {token}"})
            with urllib.request.urlopen(req, timeout=10) as resp:
                detail = json.loads(resp.read().decode()).get("data") or {}
            location = (detail.get("location") or {}).get("directory")
            if location:
                dirs.add(location)
        return dirs
    except Exception:
        return set()


def pr_list() -> list[dict]:
    proc = run(["gh", "pr", "list", "--state", "open", "--limit", "200",
                "--json", "number,title,headRefName,headRefOid,isDraft,baseRefName"])
    if proc.returncode != 0:
        sys.exit(f"pr-rebase: gh pr list failed: {proc.stderr.strip()}")
    return json.loads(proc.stdout or "[]")


def verdicts_at_head(number: int, head: str) -> bool:
    proc = run(["gh", "pr", "view", str(number), "--json", "comments"])
    if proc.returncode != 0:
        return True  # cannot prove there is no verdict: treat as gate present
    try:
        comments = json.loads(proc.stdout or "{}").get("comments") or []
    except ValueError:
        return True
    for comment in comments:
        body = comment.get("body") or ""
        for m in VERDICT_RE.finditer(body):
            if m.group(2) == head:
                return True
    return False


def behind(repo: str, branch: str) -> int:
    out = git(repo, "rev-list", "--count", f"origin/{branch}..origin/main").stdout.strip()
    return int(out or 0)


def rebase_one(repo: str, pr: dict) -> tuple[bool, str]:
    branch = pr["headRefName"]
    old = pr["headRefOid"]
    tmp = tempfile.mkdtemp(prefix="pr-rebase-", dir=tempfile.gettempdir())
    wt = os.path.join(tmp, "wt")
    try:
        proc = run(["git", "-C", repo, "worktree", "add", "--detach", wt, f"origin/{branch}"])
        if proc.returncode != 0:
            return (False, f"worktree add failed: {proc.stderr.strip()}")
        proc = run(["bash", SAFE_REBASE, "origin/main", wt], cwd=repo)
        if proc.returncode != 0:
            return (False, f"conflict: {proc.stderr.strip() or proc.stdout.strip()}")
        new = git(wt, "rev-parse", "HEAD").stdout.strip()
        if new == old:
            return (False, "already current")
        proc = run(["git", "-C", wt, "push", "--force-with-lease", "origin", f"HEAD:{branch}"])
        if proc.returncode != 0:
            return (False, f"push failed: {proc.stderr.strip()}")
        return (True, f"{old[:10]} -> {new[:10]}")
    finally:
        run(["git", "-C", repo, "worktree", "remove", "--force", wt])
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sweep", action="store_true", required=True)
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    apply = args.apply and not args.dry_run
    repo = repo_root()
    fetch = run(["git", "-C", repo, "fetch", "origin"])
    if fetch.returncode != 0:
        sys.exit(f"pr-rebase: git fetch failed: {fetch.stderr.strip()}")
    if not roster_ok(repo):
        return 1

    wts = worktrees(repo)
    checked = {w["branch"]: w["path"] for w in wts if w["branch"]}
    live_dirs = running_dirs(repo)
    live_slots = set()
    for path in live_dirs:
        base = os.path.basename(path)
        m = re.search(r"-wt-([a-z0-9]+)$", base)
        if m:
            live_slots.add(m.group(1))

    eligible, skipped, conflicted = [], [], []
    for pr in pr_list():
        branch = pr["headRefName"]
        head = pr["headRefOid"]
        reason = None
        if pr.get("isDraft"):
            reason = "draft"
        elif branch in checked:
            reason = f"head checked out at {checked[branch]}"
        elif branch.split("/")[0] in live_slots:
            reason = f"live session owns slot {branch.split('/')[0]}"
        elif behind(repo, branch) == 0:
            reason = "not behind origin/main"
        elif verdicts_at_head(pr["number"], head):
            reason = "verdict names the current head"
        if reason:
            skipped.append((pr["number"], branch, reason))
            continue
        eligible.append(pr)

    rebased = []
    if apply:
        for pr in eligible:
            ok, detail = rebase_one(repo, pr)
            if ok:
                rebased.append((pr["number"], pr["headRefName"], detail))
            elif detail.startswith("conflict"):
                conflicted.append((pr["number"], pr["headRefName"], detail))
            else:
                skipped.append((pr["number"], pr["headRefName"], detail))
    else:
        for pr in eligible:
            rebased.append((pr["number"], pr["headRefName"], "eligible (dry run)"))

    print("\nrebased:" if apply else "\neligible:")
    for number, branch, detail in rebased:
        print(f"  #{number} {branch}  {detail}")
    if not rebased:
        print("  (none)")
    print("\nskipped:")
    for number, branch, reason in skipped:
        print(f"  #{number} {branch}  {reason}")
    if not skipped:
        print("  (none)")
    print("\nconflicted:")
    for number, branch, detail in conflicted:
        print(f"  #{number} {branch}  {detail}")
    if not conflicted:
        print("  (none)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
