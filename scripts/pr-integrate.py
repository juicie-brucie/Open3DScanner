#!/usr/bin/env python3
"""pr-integrate.py — the dispatcher's integration step for a PR against a moved main.

Why this exists: builders cut a branch, land their work, and by the time the dispatcher picks
the PR up, `main` has moved. Their `PLAN.md` rows (and sometimes a shared header) are now stale.
Rebuilding by hand has cost this loop seven cycles in one night, and a hand rebuild once mixed a
changed header with old call sites and pushed a branch that did not compile.

What it does, for one PR:
  1. Takes the PR's changed files **verbatim** from its head (everything except `docs/recomp/PLAN.md`).
  2. Merges `PLAN.md` **row-aware and append-only**: a row the PR changed keeps main's title, takes
     the PR's status, and merges the notes append-only; other rows are never reverted; new rows are
     inserted where the PR put them.
  3. Builds Release (unless the PR is docs-only or `--no-build`), so an API mismatch is caught
     before the push instead of by CI.
  4. Force-pushes with a lease against the head it read. `--dry-run` stops before the push.

It never merges. After it runs, the PR needs a fresh verdict at the new head.

The note merge is the delicate part: a row's accumulated notes are the board's memory, so nothing
is ever dropped. `merge_note` states the rule and `--selftest` pins it (no git, no gh, no network).

Usage:
  scripts/pr-integrate.py <PR> [--dry-run] [--no-build] [--keep-worktree]
  scripts/pr-integrate.py --selftest        # the append-only note merge, pinned
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WORKTREE = Path.home() / "DevReposiMac" / "Open3DScanner-wt-integrate"
PLAN = "docs/recomp/PLAN.md"
ROW_RE = re.compile(r"^\| (\d+) \| (.*?) \| ([A-Z-]+) \|\s?(.*?) \|$")


def sh(*args, cwd=None, check=True, capture=True):
    r = subprocess.run(args, cwd=cwd, text=True, capture_output=capture)
    if check and r.returncode != 0:
        raise SystemExit(f"FAILED: {' '.join(args)}\n{r.stdout}\n{r.stderr}")
    return r.stdout.strip() if capture else ""


def parse_plan(text: str):
    """Board rows only: the first occurrence of each 3-digit row number."""
    rows, order = {}, []
    for line in text.split("\n"):
        m = ROW_RE.match(line)
        if m and m.group(1) not in rows:
            rows[m.group(1)] = {"line": line, "title": m.group(2), "status": m.group(3), "note": m.group(4)}
            order.append(m.group(1))
    return rows, order


def norm_title(t: str) -> str:
    return t.replace("****", "**").strip()


def _norm(s: str) -> str:
    """Whitespace-collapsed text, so 'a  b' and 'a b' compare equal."""
    return " ".join(s.split())


def _addition(after: str, base: str) -> str:
    """What `after` adds on top of `base` ("" when it adds nothing)."""
    if after == base:
        return ""
    if after.startswith(base):
        return after[len(base):].strip()
    return after.strip()


def merge_note(base: str, main: str, pr: str) -> str:
    """Three-way, append-only merge of one row's note.

    `base` is the note both sides started from; `main` and `pr` are the two sides' notes.
    The rule: main's text first, then what the PR added on top of the base — so a row both
    sides moved keeps base + main's addition + the PR's addition. Nothing is dropped and
    nothing is repeated: one side already a prefix of the other, or the same text appended
    on both sides, is recognised and not written twice.
    """
    base, main, pr = base.strip(), main.strip(), pr.strip()
    if pr == base:
        return main                      # the PR did not touch the note
    if main == base:
        return pr                       # only the PR moved the note
    if pr.startswith(main):
        return pr                       # the PR's note already carries main's text
    if main.startswith(pr):
        return main                     # main's note already carries the PR's text
    pr_add = _addition(pr, base)
    if not pr_add or _norm(pr_add) in _norm(main):
        return main                     # the PR's addition is already in main's note
    return main + " " + pr_add


def merge_plan(main_text: str, br_text: str, base_text: str) -> tuple[str, list[str]]:
    main_rows, main_order = parse_plan(main_text)
    br_rows, br_order = parse_plan(br_text)
    base_rows, _ = parse_plan(base_text)
    changes: list[str] = []
    out_lines = main_text.split("\n")

    def row_line(n: str, status: str | None, note: str | None, title: str | None) -> str:
        m = main_rows[n]
        t = title if title is not None else m["title"]
        s = status if status is not None else m["status"]
        nt = note if note is not None else m["note"]
        return f"| {n} | {t} | {s} | {nt} |"

    def replace_row(n: str, line: str):
        for i, l in enumerate(out_lines):
            if l.startswith(f"| {n} |"):
                out_lines[i] = line
                return True
        return False

    new_rows: list[tuple[str, str]] = []  # (anchor row number, line) for rows absent on main

    for n in br_order:
        b = base_rows.get(n)
        r = br_rows[n]
        m = main_rows.get(n)
        if m is None:
            # a new row: remember the row before it in the branch as its anchor
            idx = br_order.index(n)
            anchor = br_order[idx - 1] if idx > 0 else None
            new_rows.append((anchor, r["line"]))
            changes.append(f"add #{n}")
            continue
        if b is not None and b["line"] == r["line"]:
            continue  # untouched by the PR
        # title: main's. The PR's deliberate reword survives only where main left the
        # title alone — main's own rewording is never reverted.
        pr_reworded = b is not None and norm_title(b["title"]) != norm_title(r["title"])
        main_reworded = b is not None and norm_title(b["title"]) != norm_title(m["title"])
        title = r["title"] if (pr_reworded and not main_reworded) else m["title"]
        # note: append-only — main's note, then what the PR added on top of the base.
        note = merge_note(b["note"] if b is not None else "", m["note"], r["note"])
        replace_row(n, row_line(n, r["status"], note, title))
        changes.append(f"#{n} -> {r['status']}")

    for anchor, line in new_rows:
        n = ROW_RE.match(line).group(1)
        if anchor:
            pos = next((i for i, l in enumerate(out_lines) if l.startswith(f"| {anchor} |")), None)
        else:
            pos = None
        if pos is None:
            pos = max(i for i, l in enumerate(out_lines) if l.startswith("| "))
        out_lines.insert(pos + 1, line)

    return "\n".join(out_lines), changes


def _board(rows) -> str:
    """A board-shaped text for the selftest: a header and the given rows."""
    head = "# Plan\n\n| # | Task | Status | Note |\n|---|---|---|---|\n"
    return head + "".join(f"| {n} | {t} | {s} | {note} |\n" for n, t, s, note in rows)


def selftest() -> int:
    """Pin the append-only note merge. Pure: no git, no gh, no network.

    The cases are the board's memory rules — a row's accumulated notes are never
    dropped, and never written twice.
    """
    bad = []

    def merged_row(base, main, pr, row_no):
        text, _ = merge_plan(_board(main), _board(pr), _board(base))
        rows, _ = parse_plan(text)
        got = rows.get(row_no)
        return got["line"] if got else "<missing>"

    def case(name, base, main, pr, row_no, want):
        got = merged_row(base, main, pr, row_no)
        if got != want:
            bad.append(f"{name}: got {got!r}, want {want!r}")
        else:
            print(f"SELFTEST ok: {name}")

    T, N = "Title", "base"
    case("pr-only note change -> base + the PR's addition",
         [(101, T, "TODO", N)],
         [(101, T, "TODO", N)],
         [(101, T, "HW", N + " pr added")],
         "101", "| 101 | Title | HW | base pr added |")

    case("main-only note change -> main's note kept",
         [(101, T, "TODO", N)],
         [(101, T, "TODO", N + " main added")],
         [(101, T, "HW", N)],
         "101", "| 101 | Title | HW | base main added |")

    case("both changed -> base + main's addition + the PR's addition",
         [(101, T, "TODO", N)],
         [(101, T, "HW", N + " main added")],
         [(101, T, "SPEC-READY", N + " pr added")],
         "101", "| 101 | Title | SPEC-READY | base main added pr added |")

    case("the same text appended on both sides is not written twice",
         [(101, T, "TODO", N)],
         [(101, T, "TODO", N + " added")],
         [(101, T, "HW", N + " added")],
         "101", "| 101 | Title | HW | base added |")

    case("one side a prefix of the other is not duplicated",
         [(101, T, "TODO", N)],
         [(101, T, "TODO", N + " added")],
         [(101, T, "HW", N + " added more")],
         "101", "| 101 | Title | HW | base added more |")

    case("a PR that rewrote the note keeps main's addition too",
         [(101, T, "TODO", N)],
         [(101, T, "TODO", N + " main added")],
         [(101, T, "HW", "rewritten")],
         "101", "| 101 | Title | HW | base main added rewritten |")

    case("a row absent from the base keeps main's title",
         [],
         [(101, "Main title", "DONE", "main note")],
         [(101, "PR title", "HW", "pr note")],
         "101", "| 101 | Main title | HW | main note pr note |")

    case("a row the PR did not touch is never reverted",
         [(101, T, "TODO", N), (102, "Other", "TODO", "other base")],
         [(101, T, "TODO", N), (102, "Other", "DONE", "other main note")],
         [(101, T, "HW", N + " pr added"), (102, "Other", "TODO", "other base")],
         "102", "| 102 | Other | DONE | other main note |")

    # The three-way rule, stated on the note alone (the case the verifier reported).
    if merge_note("B", "B main", "B pr") != "B main pr":
        bad.append("merge_note(B, B+main, B+pr): want 'B main pr'")
    else:
        print("SELFTEST ok: merge_note keeps base + main's addition + the PR's addition")

    for b in bad:
        print("SELFTEST FAIL:", b)
    print(f"selftest: 9 cases, {'FAIL' if bad else 'ok'}")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pr", type=int, nargs="?")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--keep-worktree", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if a.pr is None:
        ap.error("a PR number is required")

    sh("git", "fetch", "-q", "origin", cwd=REPO)
    info = sh("gh", "pr", "view", str(a.pr), "--json", "headRefName,headRefOid", "--jq",
              r'"\(.headRefName) \(.headRefOid)"', cwd=REPO)
    branch, old_head = info.split()
    base = sh("git", "merge-base", f"origin/{branch}", "origin/main", cwd=REPO)
    changed = sh("git", "diff", "--name-only", f"{base}..origin/{branch}", cwd=REPO).split("\n")
    files = [f for f in changed if f and f != PLAN]
    docs_only = all(not f.endswith((".c", ".cc", ".cpp", ".h", ".hpp", ".py", ".js")) and
                    f != "CMakeLists.txt" for f in files)
    print(f"pr #{a.pr}: {branch} @ {old_head[:10]}")
    print(f"  base {base[:10]} | {len(files)} file(s) | docs_only={docs_only}")

    if not WORKTREE.exists():
        sh("git", "worktree", "add", "--detach", str(WORKTREE), "origin/main", cwd=REPO)
    if not (WORKTREE / "third_party/libigl/CMakeLists.txt").exists():
        print("  initialising submodules (one time)…")
        sh("git", "submodule", "update", "--init", "--depth", "1", cwd=WORKTREE, capture=False)

    sh("git", "checkout", "-q", "origin/main", cwd=WORKTREE)
    sh("git", "switch", "-q", "-C", f"integrate/{a.pr}", "origin/main", cwd=WORKTREE)
    for f in files:
        sh("git", "checkout", f"origin/{branch}", "--", f, cwd=WORKTREE)

    # PLAN merge (row-aware, append-only)
    main_plan = (WORKTREE / PLAN).read_text()
    br_plan = sh("git", "show", f"origin/{branch}:{PLAN}", cwd=REPO)
    base_plan = sh("git", "show", f"{base}:{PLAN}", cwd=REPO)
    merged, changes = merge_plan(main_plan, br_plan, base_plan)
    (WORKTREE / PLAN).write_text(merged)
    print("  plan:", ", ".join(changes) if changes else "(no row changes)")

    sh("git", "add", "-A", cwd=WORKTREE)
    sh("git", "commit", "-q", "-m",
       f"integrate #{a.pr} onto origin/main (code verbatim; PLAN row-aware, append-only)", cwd=WORKTREE)

    if not docs_only and not a.no_build:
        build = WORKTREE / "build-integrate"
        print("  building Release…")
        sh("cmake", "-S", ".", "-B", str(build), "-G", "Unix Makefiles", "-DCMAKE_BUILD_TYPE=Release",
           cwd=WORKTREE, capture=False)
        sh("cmake", "--build", str(build), "-j", str(os.cpu_count() or 4), cwd=WORKTREE, capture=False)

    if a.dry_run:
        print("  dry-run: not pushing")
    else:
        sh("git", "push", f"--force-with-lease={branch}:{old_head}", "origin", f"HEAD:{branch}",
           cwd=WORKTREE, capture=False)
        print(f"  pushed {branch} (re-verify at the new head)")
    if not a.keep_worktree:
        shutil.rmtree(WORKTREE / "build-integrate", ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
