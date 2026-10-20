#!/usr/bin/env python3
"""wt-roster.py — check, report and prune the fixed OpenScanner worktree roster.

The roster is the table between <!-- wt-roster:begin --> and
<!-- wt-roster:end --> in the dispatch agent file
(~/.config/opencode/agents/openscanner/dispatch.md by default).  The layout is
static: this tool never creates, removes, renames or re-points a worktree.  A
missing path is DRIFT; an extra path is a LEGACY room, reported and never bound.

Usage:
    python3 scripts/wt-roster.py check
    python3 scripts/wt-roster.py report
    python3 scripts/wt-roster.py prune --dry-run
    python3 scripts/wt-roster.py prune --apply

Exit codes:
    check   0 PASS, 1 DRIFT, 2 UNVERIFIED (server/roster unreadable)
    report  0 (a report is not a gate)
    prune   0 ok, 1 refused (check is DRIFT) or unsafe classification

The server is the source of truth for a session's directory and for which
sessions are running.  If it cannot be reached the check prints UNVERIFIED,
never PASS.  Credentials are read at runtime from service.json and are never
printed.

Safety rules for prune (dispatch.md, "Branch pruning"):
  * never delete main, a roster base, a branch checked out in a worktree, an
    open PR head, or a branch under a live/dirty slot;
  * delete only: an ancestor of origin/main, a merged PR's head, or a head
    reachable from a surviving ref;
  * everything else is "decide" — reported, never deleted here.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from collections import defaultdict

HOME = os.path.expanduser("~")
DEFAULT_AGENT_MD = os.path.join(HOME, ".config", "opencode", "agents", "openscanner", "dispatch.md")
SERVICE = os.environ.get("OPENSCANNER_SERVICE", os.path.join(HOME, ".config", "opencode", "service.json"))
DEFAULT_TOKENS = 0  # report only; check-agents.py owns the threshold

BEGIN = "<!-- wt-roster:begin -->"
END = "<!-- wt-roster:end -->"


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
        sys.exit("wt-roster: not inside a git worktree")
    return out


def parse_roster(path: str) -> list[dict]:
    try:
        text = open(path, encoding="utf-8").read()
    except OSError as exc:
        sys.exit(f"wt-roster: cannot read roster file {path}: {exc}")
    if BEGIN not in text or END not in text:
        sys.exit(f"wt-roster: roster markers not found in {path}")
    block = text.split(BEGIN, 1)[1].split(END, 1)[0]
    rows = []
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("|") or "---" in line:
            continue
        cells = [c.strip().strip("`") for c in line.strip("|").split("|")]
        if len(cells) < 5 or cells[0] == "slot":
            continue
        slot, path, base, agent, owner = cells[:5]
        if path.startswith("~"):
            path = os.path.expanduser(path)
        elif not os.path.isabs(path):
            path = os.path.join(HOME, path)
        rows.append({
            "slot": slot,
            "path": os.path.expanduser(path),
            "base": base,
            "agent": agent,
            "owner": owner,
        })
    if not rows:
        sys.exit(f"wt-roster: no rows parsed from {path}")
    return rows


def worktrees(repo: str) -> list[dict]:
    out = git(repo, "worktree", "list", "--porcelain", check=True).stdout
    result = []
    for block in out.split("\n\n"):
        if not block.strip():
            continue
        item = {"path": "", "branch": None, "head": None, "detached": False}
        for line in block.splitlines():
            if line.startswith("worktree "):
                item["path"] = line[len("worktree "):]
            elif line.startswith("HEAD "):
                item["head"] = line[len("HEAD "):]
            elif line.startswith("branch "):
                item["branch"] = line[len("branch refs/heads/"):]
            elif line == "detached":
                item["detached"] = True
        result.append(item)
    return result


def branch_list(repo: str) -> list[str]:
    out = git(repo, "for-each-ref", "--format=%(refname:short)", "refs/heads").stdout
    return [b for b in out.splitlines() if b]


def ahead_behind(repo: str, branch: str) -> tuple[int, int]:
    """Return (ahead of origin/main, behind origin/main)."""
    if not branch:
        return (0, 0)
    ahead = run(["git", "-C", repo, "rev-list", "--count", f"origin/main..{branch}"]).stdout.strip()
    behind = run(["git", "-C", repo, "rev-list", "--count", f"{branch}..origin/main"]).stdout.strip()
    return (int(ahead or 0), int(behind or 0))


def dirty(path: str) -> bool:
    return bool(git(path, "status", "--porcelain").stdout.strip())


# --------------------------------------------------------------------------- server
def server_port() -> int | None:
    base = os.environ.get("OPENCODE_BASE")
    if base:
        m = re.search(r":(\d+)", base)
        return int(m.group(1)) if m else None
    try:
        out = run(["lsof", "-nP", "-iTCP", "-sTCP:LISTEN"]).stdout
    except OSError:
        return None
    for line in out.splitlines():
        if "opencode" in line:
            m = re.search(r":(\d+)\b", line)
            if m:
                return int(m.group(1))
    return None


def api(path: str):
    port = server_port()
    if not port:
        raise ConnectionError("no local OpenCode listener")
    try:
        password = json.load(open(SERVICE, encoding="utf-8")).get("password")
    except OSError as exc:
        raise ConnectionError(f"cannot read service credentials: {exc}") from exc
    if not password:
        raise ConnectionError("no password in service credentials")
    token = base64.b64encode(f"opencode:{password}".encode()).decode()
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", headers={"Authorization": f"Basic {token}"})
    with urllib.request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode())


def sessions() -> tuple[list[dict], set[str], str | None]:
    """Return (primary session records, running ids, error)."""
    try:
        raw = api("/api/session")
        active = api("/api/session/active")
    except (ConnectionError, urllib.error.URLError, OSError, ValueError) as exc:
        return ([], set(), str(exc))
    data = raw.get("data", raw)
    if not isinstance(data, list):
        return ([], set(), "unexpected /api/session shape")
    running = set((active.get("data") or {}).keys())
    records = []
    for item in data:
        if item.get("parentID"):
            continue
        sid = item.get("id")
        if not sid:
            continue
        try:
            detail = api(f"/api/session/{sid}").get("data", {})
        except Exception:
            detail = item
        location = (detail.get("location") or {}).get("directory") or ""
        model = detail.get("model") or {}
        tokens = detail.get("tokens") or {}
        records.append({
            "id": sid,
            "title": detail.get("title") or item.get("title") or "",
            "agent": detail.get("agent") or item.get("agent") or "",
            "model": model,
            "directory": location,
            "created": (detail.get("time") or {}).get("created") or 0,
            "updated": (detail.get("time") or {}).get("updated") or 0,
            "context": (tokens.get("input") or 0) + ((tokens.get("cache") or {}).get("read") or 0),
            "input": tokens.get("input") or 0,
        })
    return (records, running, None)


def model_text(model: dict) -> str:
    if not model:
        return "-"
    text = f"{model.get('providerID', '?')}/{model.get('id', '?')}"
    if model.get("variant"):
        text += f"#{model['variant']}"
    return text


def plan_row(repo: str, title: str) -> tuple[str, str]:
    m = re.search(r"#(\d+)\b", title or "")
    if not m:
        return ("-", "-")
    number = m.group(1)
    text = git(repo, "show", "origin/main:docs/recomp/PLAN.md").stdout
    for line in text.splitlines():
        if line.startswith(f"| {number} |"):
            cells = [c.strip() for c in line.strip("|").split("|")]
            return (number, cells[2] if len(cells) > 2 else "?")
    return (number, "missing")


# --------------------------------------------------------------------------- check
def collect(repo: str, roster: list[dict]):
    wts = worktrees(repo)
    by_path = {w["path"]: w for w in wts}
    branches = branch_list(repo)
    records, running, err = sessions()
    by_dir = defaultdict(list)
    for rec in records:
        if rec["directory"]:
            by_dir[rec["directory"]].append(rec)
    for recs in by_dir.values():
        recs.sort(key=lambda r: r["created"], reverse=True)

    rows = []
    drift = []
    for entry in roster:
        slot, path = entry["slot"], entry["path"]
        wt = by_path.get(path)
        row = {"slot": slot, "path": path, "base": entry["base"], "agent": entry["agent"],
               "owner": entry["owner"], "branch": None, "detached": False, "dirty": False,
               "ahead": 0, "behind": 0, "session": None, "legacy_records": [],
               "warnings": [], "drift": []}
        if not wt:
            drift.append(f"{slot}: path missing ({path})")
            row["drift"].append("path missing")
            rows.append(row)
            continue
        row["branch"] = wt["branch"]
        row["detached"] = wt["detached"]
        row["dirty"] = dirty(path)
        if wt["branch"]:
            row["ahead"], row["behind"] = ahead_behind(repo, wt["branch"])
        branch = wt["branch"] or ""
        base_ns = entry["base"].split("/", 1)[0] + "/"
        allowed = branch in (entry["base"],) or branch.startswith(slot + "/") or branch.startswith(base_ns)
        if slot.startswith("verify") and wt["detached"]:
            allowed = True
        if not allowed:
            drift.append(f"{slot}: branch {wt['branch'] or 'detached'} is outside {slot}/ (base {entry['base']})")
            row["drift"].append("branch outside namespace")
        if entry["base"] not in branches and not wt["detached"]:
            drift.append(f"{slot}: base branch {entry['base']} does not exist")
            row["drift"].append("base missing")

        recs = by_dir.get(path, [])
        row["legacy_records"] = recs[1:]
        if entry["agent"].startswith("external") or slot == "hw":
            row["session"] = None
            row["warnings"].append("external owner (no OpenCode session)")
        elif not recs:
            if row["dirty"]:
                row["warnings"].append("unbound (dirty slot; bind after it is clean)")
            else:
                drift.append(f"{slot}: no session bound in {path}")
                row["drift"].append("no session")
        else:
            primary = recs[0]
            row["session"] = primary
            if primary["agent"] != entry["agent"]:
                drift.append(f"{slot}: primary session {primary['id']} has agent {primary['agent']}, expected {entry['agent']}")
                row["drift"].append("agent mismatch")
            live = [r for r in recs if r["id"] in running]
            if len(live) > 1:
                drift.append(f"{slot}: {len(live)} sessions running in {path} ({', '.join(r['id'] for r in live)})")
                row["drift"].append("two live sessions")
            if row["dirty"]:
                row["warnings"].append("dirty (not reused)")
        rows.append(row)

    roster_paths = {e["path"] for e in roster}
    legacy = [w for w in wts if w["path"] not in roster_paths]
    return rows, legacy, drift, err, running


def cmd_check(repo: str, roster: list[dict]) -> int:
    rows, legacy, drift, err, running = collect(repo, roster)
    if err:
        print(f"UNVERIFIED — the session server could not be queried: {err}")
        print("             (a PASS needs a live /api/session; this is never treated as PASS)")
        return 2
    for row in rows:
        flags = []
        if row["drift"]:
            flags.append("DRIFT: " + ", ".join(row["drift"]))
        if row["warnings"]:
            flags.append("; ".join(row["warnings"]))
        if flags:
            print(f"  {row['slot']:<9} {'; '.join(flags)}")
    if legacy:
        print(f"  LEGACY    {len(legacy)} worktree(s) outside the roster (reported, never bound):")
        for wt in legacy:
            print(f"            {wt['path']}  [{wt['branch'] or 'detached'}]")
    if drift:
        print(f"DRIFT — {len(drift)} violation(s); no dispatch until resolved")
        return 1
    print(f"PASS — {len(rows)} roster slots, {len(legacy)} legacy room(s), {len(running)} running session(s)")
    return 0


def cmd_report(repo: str, roster: list[dict]) -> int:
    rows, legacy, drift, err, running = collect(repo, roster)
    if err:
        print(f"server: UNVERIFIED ({err})")
    print("| slot | path | branch | ahead/behind | dirty | session | title | agent/model | ctx | row |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    swarm = []
    for row in rows:
        rec = row["session"]
        number, status = plan_row(repo, rec["title"]) if rec else ("-", "-")
        print("| {slot} | {path} | {branch} | {ahead}/{behind} | {dirty} | {sid} | {title} | {agent} {model} | {ctx} | {number} {status} |".format(
            slot=row["slot"], path=row["path"].replace(HOME, "~"),
            branch=row["branch"] or ("detached" if row["detached"] else "-"),
            ahead=row["ahead"], behind=row["behind"], dirty="yes" if row["dirty"] else "no",
            sid=rec["id"] if rec else "-", title=(rec["title"] if rec else "-").replace("|", "/")[:70],
            agent=(rec["agent"] if rec else "-"), model=model_text(rec["model"] if rec else {}),
            ctx=f"{rec['context']:,}" if rec else "-", number=number, status=status))
        if rec:
            swarm.append(f"{row['slot']} -> {rec['id']}")
    print()
    print("swarm: " + " | ".join(swarm))
    if legacy:
        print(f"legacy rooms: {len(legacy)} (drained by the user; never bound)")
    if drift:
        print(f"drift: {len(drift)}")
    return 0


# --------------------------------------------------------------------------- prune
def gh_prs() -> tuple[list[dict], str | None]:
    proc = run(["gh", "pr", "list", "--state", "all", "--limit", "1000",
                "--json", "number,state,headRefName,headRefOid,mergedAt"])
    if proc.returncode != 0:
        return ([], proc.stderr.strip() or "gh pr list failed")
    try:
        return (json.loads(proc.stdout or "[]"), None)
    except ValueError as exc:
        return ([], f"gh returned invalid JSON: {exc}")


def classify_refs(repo: str, refs: list[tuple[str, str]], keep_names: set[str],
                  open_heads: set[str], merged_heads: set[str]):
    buckets = {"ancestor": [], "merged": [], "reachable": [], "protected": [], "decide": []}
    for name, sha in refs:
        if not name or name == "HEAD" or name == "main":
            continue
        if name in keep_names:
            buckets["protected"].append((name, sha, "checked-out / dirty / roster base"))
            continue
        if sha in open_heads:
            buckets["protected"].append((name, sha, "open PR head"))
            continue
        if run(["git", "-C", repo, "merge-base", "--is-ancestor", sha, "origin/main"]).returncode == 0:
            buckets["ancestor"].append((name, sha, "ancestor of origin/main"))
            continue
        if sha in merged_heads:
            buckets["merged"].append((name, sha, "merged PR head"))
            continue
        # Reachable from a ref that will survive: origin/main or a protected
        # name.  A ref we are deleting is NOT a survivor; this keeps the
        # bucket conservative and sends ambiguous duplicates to "decide".
        contains = run(["git", "-C", repo, "for-each-ref", "--contains", sha,
                        "--format=%(refname:short)", "refs/heads", "refs/remotes/origin"]).stdout.split()
        survivors = []
        for c in contains:
            short = c.removeprefix("origin/")
            if c == "origin/main" or (short in keep_names and short != name):
                survivors.append(c)
        if survivors:
            buckets["reachable"].append((name, sha, survivors[0]))
            continue
        buckets["decide"].append((name, sha, "unclassified — the user decides"))
    return buckets


def cmd_prune(repo: str, roster: list[dict], apply: bool) -> int:
    rows, legacy, drift, err, running = collect(repo, roster)
    if err:
        print(f"prune: session server UNVERIFIED ({err}); classification is still safe, but check first.")
    if drift:
        print("prune: REFUSING — check is DRIFT. Fix the layout before deleting anything.")
        return 1
    prs, gh_err = gh_prs()
    if gh_err:
        print(f"prune: REFUSING — cannot classify open/merged PR heads: {gh_err}")
        return 1
    open_heads = {p["headRefOid"] for p in prs if p["state"] == "OPEN"}
    merged_heads = {p["headRefOid"] for p in prs if p["state"] == "MERGED"}
    wts = worktrees(repo)
    checked = {w["branch"] for w in wts if w["branch"]}
    dirty_branches = {w["branch"] for w in wts if w["branch"] and dirty(w["path"])}
    keep_names = checked | dirty_branches | {e["base"] for e in roster} | {"main"}
    roster_bases = {e["base"] for e in roster}

    print(f"prune: {len(open_heads)} open PR head(s), {len(merged_heads)} merged PR head(s), "
          f"{len(keep_names)} protected name(s)")

    local_refs = []
    for name in branch_list(repo):
        sha = git(repo, "rev-parse", name).stdout.strip()
        local_refs.append((name, sha))
    remote_refs = []
    for line in git(repo, "for-each-ref", "--format=%(refname:short) %(objectname)",
                    "refs/remotes/origin").stdout.splitlines():
        name, sha = line.rsplit(" ", 1)
        name = name.removeprefix("origin/")
        if name != "HEAD":
            remote_refs.append((name, sha))

    local = classify_refs(repo, local_refs, keep_names, open_heads, merged_heads)
    remote = classify_refs(repo, remote_refs, keep_names, open_heads, merged_heads)

    def show(label, buckets):
        print(f"\n{label}:")
        for key in ("ancestor", "merged", "reachable", "protected", "decide"):
            print(f"  {key:<9} {len(buckets[key])}")
        for name, sha, why in buckets["decide"]:
            print(f"    decide  {name}  {sha[:10]}  {why}")

    show("local branches", local)
    show("remote branches", remote)

    if not apply:
        print("\nprune: dry run — nothing deleted. Re-run with --apply to delete the safe buckets.")
        return 0

    deleted_local, deleted_remote, failed = [], [], []
    for key in ("ancestor", "merged", "reachable"):
        for name, sha, _ in local[key]:
            flag = "-d" if key == "ancestor" else "-D"
            proc = git(repo, "branch", flag, name)
            (deleted_local if proc.returncode == 0 else failed).append(name if proc.returncode == 0 else (name, proc.stderr.strip()))
    remote_safe = [n for key in ("ancestor", "merged", "reachable") for n, _, _ in remote[key]]
    for i in range(0, len(remote_safe), 40):
        batch = remote_safe[i:i + 40]
        proc = run(["git", "-C", repo, "push", "origin", "--delete", *batch])
        if proc.returncode == 0:
            deleted_remote += batch
        else:
            for name in batch:
                p = run(["git", "-C", repo, "push", "origin", "--delete", name])
                (deleted_remote if p.returncode == 0 else failed).append(
                    name if p.returncode == 0 else (name, p.stderr.strip().splitlines()[-1] if p.stderr.strip() else "?"))
    run(["git", "-C", repo, "remote", "prune", "origin"])
    print(f"\nprune: deleted {len(deleted_local)} local, {len(deleted_remote)} remote; {len(failed)} failed")
    for item in failed:
        print(f"  FAILED {item}")
    return 0 if not failed else 1


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("command", choices=["check", "report", "prune"])
    ap.add_argument("--roster", default=os.environ.get("OPENSCANNER_DISPATCH_MD", DEFAULT_AGENT_MD))
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    repo = repo_root()
    roster = parse_roster(args.roster)
    if args.command == "check":
        return cmd_check(repo, roster)
    if args.command == "report":
        return cmd_report(repo, roster)
    return cmd_prune(repo, roster, args.apply and not args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
