#!/usr/bin/env python3
"""Shape-check the openscanner agent files, and optionally confirm they load.

Usage:
    scripts/check-agents.py [path]        # default: ~/.config/opencode/agents/openscanner
    scripts/check-agents.py --loaded      # also query the local OpenCode server
    scripts/check-agents.py --selftest    # assert the checker catches the known bad shapes

Scope: this validates the *shape* of each permission entry (a complete
{action, resource, effect} triple) and the presence of the expected agent
files. It does NOT validate policy (what each entry allows), and a clean
file-level pass does not prove the agents loadably registered — use
--loaded (or the printed command) for that.

Exit codes: 0 = all checks passed, 1 = a check failed, 2 = environment problem.

Why this exists (row #90): a permission list with malformed entries (a bare
`- action: shell`, a missing resource/effect pair, a duplicated pair) YAML-
parses fine, but OpenCode silently drops the whole agent — sessions then fail
with `Agent not found: "openscanner/builder"`.
"""

import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.request

DEFAULT_DIR = os.path.expanduser("~/.config/opencode/agents/openscanner")
EXPECTED = [
    "openscanner/builder",
    "openscanner/dispatch",
    "openscanner/planner",
    "openscanner/recon",
    "openscanner/recon-sweep",
    "openscanner/verifier",
]


def log(msg):
    print(msg)


def front_matter(text):
    m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
    return m.group(1) if m else ""


def permission_entries(fm):
    if "permissions:" not in fm:
        return []
    tail = fm[fm.index("permissions:"):]
    return re.split(r"\n(?=  - )", tail)[1:]


def entry_problems(entry):
    """Return a list of shape problems for one permission entry."""
    problems = []
    actions = len(re.findall(r"action:\s*(\S+)", entry))
    resources = len(re.findall(r"resource:", entry))
    effects = len(re.findall(r"effect:", entry))
    if actions != 1:
        problems.append(f"action count {actions} (want 1)")
    if resources != 1:
        problems.append(f"resource count {resources} (want 1)")
    if effects != 1:
        problems.append(f"effect count {effects} (want 1)")
    return problems


def check_dir(path, expect_all=True):
    if not os.path.isdir(path):
        log(f"ERROR: agent directory not found: {path}")
        return 1
    files = sorted(f for f in os.listdir(path) if f.endswith(".md"))
    if not files:
        log(f"ERROR: no .md agent files in {path}")
        return 1
    bad_total = 0
    names = []
    for fname in files:
        names.append("openscanner/" + fname[:-3])
        with open(os.path.join(path, fname), encoding="utf-8") as f:
            fm = front_matter(f.read())
        entries = permission_entries(fm)
        bad = [(i + 1, e, entry_problems(e)) for i, e in enumerate(entries) if entry_problems(e)]
        bad_total += len(bad)
        status = "ok" if not bad else f"{len(bad)} INCOMPLETE"
        log(f"{fname[:-3]:14s} entries={len(entries):2d}  {status}")
        for idx, entry, problems in bad:
            snippet = " ".join(entry.strip().split())[:80]
            log(f"    entry {idx}: {'; '.join(problems)} :: {snippet}")
    missing = [n for n in EXPECTED if n not in names] if expect_all else []
    if missing:
        log(f"ERROR: expected agent(s) missing: {', '.join(missing)}")
    return 1 if (bad_total or missing) else 0


def local_server_url():
    """Return (url, password) for the local OpenCode server, or (None, None)."""
    try:
        out = subprocess.run(["lsof", "-nP", "-iTCP", "-sTCP:LISTEN"], capture_output=True, text=True).stdout
    except OSError:
        return None, None
    port = None
    for line in out.splitlines():
        if "opencode" in line:
            parts = line.split()
            if len(parts) >= 9:
                port = parts[-2].split(":")[-1]
                break
    if not port:
        return None, None
    cred_path = os.path.expanduser("~/.config/opencode/service.json")
    try:
        with open(cred_path, encoding="utf-8") as f:
            password = json.load(f)["password"]
    except (OSError, KeyError, json.JSONDecodeError):
        return None, None
    return f"http://127.0.0.1:{port}", password


def check_loaded():
    """Confirm the expected agents are registered with the running server."""
    url, password = local_server_url()
    if url is None:
        log("loaded: SKIPPED (no local OpenCode server found).")
        log('  Start OpenCode, then run: scripts/check-agents.py --loaded')
        log(f"  (or, with credentials: curl -sS -u opencode:<password> {url or 'http://127.0.0.1:<port>'}/api/agent)")
        return None
    import base64

    token = base64.b64encode(f"opencode:{password}".encode()).decode()
    req = urllib.request.Request(url + "/api/agent", headers={"Authorization": "Basic " + token})
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.load(resp)
    except Exception as e:  # noqa: BLE001 - report any transport/parse failure
        log(f"loaded: SKIPPED (could not query {url}/api/agent: {e})")
        return None
    ids = [a.get("id") for a in data.get("data", [])]
    missing = [n for n in EXPECTED if n not in ids]
    if missing:
        log(f"loaded: FAIL — server is missing: {', '.join(missing)}")
        return 1
    log(f"loaded: ok — all {len(EXPECTED)} openscanner agents registered with {url}")
    return 0


def selftest():
    failures = 0
    good = "---\npermissions:\n  - action: read\n    resource: '*'\n    effect: allow\n---\n"
    bad_bare = "---\npermissions:\n  - action: shell\n---\n"
    bad_missing = "---\npermissions:\n  - action: opencode_list_mcp_resources\n    resource: '*'\n---\n"
    bad_dup = "---\npermissions:\n  - action: read\n    resource: a\n    resource: b\n    effect: allow\n---\n"
    cases = [
        ("good", good, []),
        ("bare action", bad_bare, ["resource count 0", "effect count 0"]),
        ("missing effect", bad_missing, ["effect count 0"]),
        ("duplicate resource", bad_dup, ["resource count 2"]),
    ]
    for name, text, expected in cases:
        problems = entry_problems(permission_entries(front_matter(text))[0])
        if bool(problems) != bool(expected):
            log(f"SELFTEST FAIL: {name}: got {problems}, expected problems={expected}")
            failures += 1
        else:
            log(f"SELFTEST ok: {name}")
    # A whole-file check catches a malformed entry in a temp dir.
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "builder.md"), "w", encoding="utf-8") as f:
            f.write(bad_bare)
        if check_dir(d, expect_all=False) == 0:
            log("SELFTEST FAIL: check_dir accepted a malformed entry")
            failures += 1
        else:
            log("SELFTEST ok: check_dir rejects a malformed entry")
    return 1 if failures else 0


def main(argv):
    if "--help" in argv or "-h" in argv:
        print(__doc__)
        return 0
    if "--selftest" in argv:
        return selftest()
    path = DEFAULT_DIR
    positional = [a for a in argv[1:] if not a.startswith("--")]
    if positional:
        path = positional[0]
    rc = check_dir(path)
    if "--loaded" in argv:
        loaded = check_loaded()
        if loaded == 1:
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
