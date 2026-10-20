#!/usr/bin/env python3
"""Check the openscanner agent files, and optionally the running sessions.

Usage:
    scripts/check-agents.py [path]        # default: ~/.config/opencode/agents/openscanner
    scripts/check-agents.py --loaded      # also confirm the six agents are registered
    scripts/check-agents.py --sessions    # also report the live sessions: model drift, context size
    scripts/check-agents.py --fix         # switch drifting *live* sessions to their agent's model
    scripts/check-agents.py --selftest    # assert the checker catches the known classes

Environment:
    OPENSCANNER_TOKEN_THRESHOLD     input-token flag for a live session (default 1000000)
    OPENSCANNER_SESSION_LIVE_MINUTES how recently a session must be updated to count as live
                                    (default 30)

Scope: this validates *configuration*, not policy. It checks the shape of each
permission entry (a complete {action, resource, effect} triple), that the
expected agent files are present, that the six agents are registered with the
running server (--loaded), and that each live session's model matches its
agent's configured model (--sessions). It does NOT validate what any permission
entry allows, and a clean pass does not prove a session behaves as configured.

Exit codes: 0 = all checks passed, 1 = a check failed, 2 = environment problem.
A skipped network check (no local server) is not a failure and does not change
the exit code.

Why this exists (row #90): a permission list with malformed entries (a bare
`- action: shell`, a missing resource/effect pair, a duplicated pair) YAML-
parses fine, but OpenCode silently drops the whole agent — sessions then fail
with `Agent not found: "openscanner/builder"`.

Why the session check exists (row #104): a session captures its model at
creation and never re-reads the agent file, so an agent whose model is later
edited keeps running the model its *sessions* were born with — the file (and
`/api/agent`) can be right while the session drifts. --sessions reports the
drift; --fix switches each drifting session via
`POST /api/session/{sessionID}/model` with `{"model": {...}}` (returns 204).

What --sessions reports (row #109):
  * a session whose record carries no model of its own (`model: null`) runs the
    AGENT's configured model — that is what the record means — so it is
    rendered as "default (agent)" and is NOT drift;
  * the live sessions are listed with their input-token count, flagged OVER
    above OPENSCANNER_TOKEN_THRESHOLD (default 1000000) — a session's context
    degrades with use, and the symptom is invisible until it speaks nonsense;
  * LIVE means: the server still holds the session (/api/session/active) or the
    session worked recently — `time.idle` is unset (it is running now) or
    within OPENSCANNER_SESSION_LIVE_MINUTES (default 30). `time.idle` is the
    work marker; `time.updated` is not (it also moves when a record is
    touched without work). Only live sessions are flagged for drift or for
    size; older sessions are history (a session captures its model at
    creation, so old drift is expected) and are counted, never flagged.

The threshold is a FLAG, not a failure: only a live model drift makes the
check fail (exit 1), so a busy board does not report a failure for sessions
that are merely large.
"""

import base64
import http.server
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

DEFAULT_DIR = os.path.expanduser("~/.config/opencode/agents/openscanner")
DEFAULT_TOKEN_THRESHOLD = 1000000
DEFAULT_LIVE_MINUTES = 30
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
    """Return (url, password) for the local OpenCode server, or (None, None).

    The password is read at runtime from the service file and returned to the
    caller only; it is never printed or logged.
    """
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


def _auth_header(password):
    token = base64.b64encode(f"opencode:{password}".encode()).decode()
    return "Basic " + token


def api_request(url, password, method, path, body=None, timeout=10):
    """Perform one authenticated request; return (parsed_json_or_None, status)."""
    headers = {"Authorization": _auth_header(password)}
    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url + path, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read()
        try:
            return (json.loads(raw) if raw else None), resp.status
        except json.JSONDecodeError:
            return None, resp.status


def as_list(payload):
    """The API wraps collections as {"data": [...]}; some paths return a bare list."""
    if isinstance(payload, dict):
        data = payload.get("data")
        return data if isinstance(data, list) else []
    return payload if isinstance(payload, list) else []


def model_key(model):
    """The comparable identity of a model config: (id, providerID, variant)."""
    if not isinstance(model, dict):
        return None
    return (model.get("id"), model.get("providerID"), model.get("variant") or "")


def format_model(model):
    """Render a model config as `providerID/id#variant` (variant omitted if empty)."""
    if not isinstance(model, dict) or not model.get("id"):
        return "<none>"
    text = f"{model.get('providerID')}/{model.get('id')}"
    if model.get("variant"):
        text += f"#{model['variant']}"
    return text


def session_model_is_default(session):
    """True when the session record carries no model of its own.

    A null (or empty) model in the record means the session runs the AGENT's
    configured model — that is what the record means (row #109's checker
    note) — so it is not drift.
    """
    model = session.get("model") if isinstance(session, dict) else None
    return not isinstance(model, dict) or not model.get("id")


def format_session_model(session):
    """Render a session's model; the agent's default is named as such."""
    if session_model_is_default(session):
        return "default (agent)"
    return format_model(session.get("model"))


def token_threshold():
    """The input-token threshold a live session is flagged over."""
    raw = (os.environ.get("OPENSCANNER_TOKEN_THRESHOLD") or "").strip()
    if not raw:
        return DEFAULT_TOKEN_THRESHOLD
    try:
        value = int(raw)
    except ValueError:
        log(f"sessions: OPENSCANNER_TOKEN_THRESHOLD '{raw}' is not an integer; using {DEFAULT_TOKEN_THRESHOLD}")
        return DEFAULT_TOKEN_THRESHOLD
    if value < 0:
        log(f"sessions: OPENSCANNER_TOKEN_THRESHOLD '{raw}' is negative; using {DEFAULT_TOKEN_THRESHOLD}")
        return DEFAULT_TOKEN_THRESHOLD
    return value


def live_minutes():
    """How recently a session must be updated to count as live."""
    raw = (os.environ.get("OPENSCANNER_SESSION_LIVE_MINUTES") or "").strip()
    if not raw:
        return DEFAULT_LIVE_MINUTES
    try:
        value = int(raw)
    except ValueError:
        log(f"sessions: OPENSCANNER_SESSION_LIVE_MINUTES '{raw}' is not an integer; using {DEFAULT_LIVE_MINUTES}")
        return DEFAULT_LIVE_MINUTES
    if value < 0:
        return DEFAULT_LIVE_MINUTES
    return value


def session_input_tokens(session):
    """The session's input-token count (0 when the record has none)."""
    tokens = session.get("tokens") if isinstance(session, dict) else None
    if not isinstance(tokens, dict):
        return 0
    value = tokens.get("input")
    return value if isinstance(value, int) else 0


def is_over_threshold(input_tokens, threshold):
    """True when a session's input tokens are over the threshold.

    Strictly greater: a session exactly at the threshold is not over it.
    """
    return input_tokens > threshold


def split_live(sessions, active_ids, minutes, now_ms=None):
    """Split the sessions into (live, history).

    LIVE means: the server still holds the session (its id is in
    `active_ids`), OR the session worked recently — `time.idle` is unset (it
    is running now: it never went idle) or within `minutes` of now.

    `time.idle` is the work marker; `time.updated` is NOT (it also moves when
    a record is touched without work — a model switch, a title change), so a
    session whose record was touched a minute ago but which has not worked for
    hours is history, not live work. Everything else is history too: a
    session that ended long ago is not live work, and the model it captured
    at creation is not drift to fix. An archived session is always history.

    Pure — no I/O — so the selftest can drive it with fixtures.
    """
    if now_ms is None:
        now_ms = int(time.time() * 1000)
    window_ms = max(0, int(minutes)) * 60 * 1000
    live, history = [], []
    for session in sessions:
        if not isinstance(session, dict):
            continue
        sid = session.get("id")
        time_fields = session.get("time") or {}
        idle = time_fields.get("idle") or 0
        archived = session.get("archived") or time_fields.get("archived")
        worked_recently = (not idle) or ((now_ms - idle) <= window_ms)
        if not archived and (sid in active_ids or worked_recently):
            live.append(session)
        else:
            history.append(session)
    return live, history


def model_mismatches(sessions, agents):
    """Return [(session, expected_model)] for openscanner sessions whose running
    model differs from their agent's configured model.

    Pure — no I/O — so the selftest can drive it with fixtures. Sessions whose
    agent is not one of the six, archived sessions, and agents the server does
    not report are ignored (the last is --loaded's job). A session whose record
    carries no model of its own runs the agent's default (row #109's checker
    note), so it is never a mismatch.
    """
    expected = {a.get("id"): a.get("model") for a in agents if isinstance(a, dict) and a.get("id")}
    out = []
    for session in sessions:
        if not isinstance(session, dict):
            continue
        agent = session.get("agent")
        if agent not in EXPECTED:
            continue
        if session.get("archived") or (isinstance(session.get("time"), dict) and session["time"].get("archived")):
            continue
        want = expected.get(agent)
        if want is None:
            continue
        if session_model_is_default(session):
            continue
        if model_key(session.get("model")) != model_key(want):
            out.append((session, want))
    return out


def check_loaded():
    """Confirm the expected agents are registered with the running server."""
    url, password = local_server_url()
    if url is None:
        log("loaded: SKIPPED (no local OpenCode server found).")
        log('  Start OpenCode, then run: scripts/check-agents.py --loaded')
        log(f"  (or, with credentials: curl -sS -u opencode:<password> {url or 'http://127.0.0.1:<port>'}/api/agent)")
        return None
    try:
        data, _ = api_request(url, password, "GET", "/api/agent")
    except Exception as e:  # noqa: BLE001 - report any transport/parse failure
        log(f"loaded: SKIPPED (could not query {url}/api/agent: {e})")
        return None
    ids = [a.get("id") for a in as_list(data)]
    missing = [n for n in EXPECTED if n not in ids]
    if missing:
        log(f"loaded: FAIL — server is missing: {', '.join(missing)}")
        return 1
    log(f"loaded: ok — all {len(EXPECTED)} openscanner agents registered with {url}")
    return 0


def fetch_sessions_agents(url, password):
    agents, _ = api_request(url, password, "GET", "/api/agent")
    sessions, _ = api_request(url, password, "GET", "/api/session")
    return as_list(sessions), as_list(agents)


def active_session_ids(url, password):
    """The ids the server still holds (/api/session/active).

    Empty when the endpoint is missing or fails: the recency rule then decides
    what is live on its own.
    """
    try:
        data, _ = api_request(url, password, "GET", "/api/session/active")
    except Exception:  # noqa: BLE001 - the recency rule still works without it
        return set()
    if not isinstance(data, dict):
        return set()
    payload = data.get("data")
    if isinstance(payload, dict):
        return set(payload.keys())
    if isinstance(payload, list):
        return {s.get("id") for s in payload if isinstance(s, dict)}
    return set()


def check_sessions(do_fix=False):
    """Report the live sessions: their model, their context size, and what is
    history — and with do_fix, switch the live sessions whose model drifts.

    Returns 0 (clean or fixed), 1 (a live session drifts, or drift remains
    after --fix), or None (skipped: no server).
    """
    url, password = local_server_url()
    if url is None:
        log("sessions: SKIPPED (no local OpenCode server found).")
        log('  Start OpenCode, then run: scripts/check-agents.py --sessions')
        return None
    try:
        sessions, agents = fetch_sessions_agents(url, password)
    except Exception as e:  # noqa: BLE001 - report any transport/parse failure
        log(f"sessions: SKIPPED (could not query {url}: {e})")
        return None

    threshold = token_threshold()
    minutes = live_minutes()
    live, history = split_live(sessions, active_session_ids(url, password), minutes)
    log(f"sessions: {len(live)} live session(s) (the server holds it, or it worked in the last {minutes} min); "
        f"{len(history)} older")

    over = []
    for session in live:
        tokens = session_input_tokens(session)
        flag = ""
        if is_over_threshold(tokens, threshold):
            flag = f"  OVER (> {threshold})"
            over.append(session)
        log(f"sessions: LIVE {session.get('id')}  {session.get('agent')}  \"{session.get('title')}\"  "
            f"model={format_session_model(session)}  tokens.input={tokens}{flag}")

    mismatches = model_mismatches(live, agents)
    for session, want in mismatches:
        log(f"sessions: DRIFT {session.get('id')}  {session.get('agent')}  \"{session.get('title')}\"")
        log(f"    running:  {format_model(session.get('model'))}")
        log(f"    expected: {format_model(want)}")
        log(f"    tokens:   input={session_input_tokens(session)}")

    history_drift = model_mismatches(history, agents)
    if history_drift:
        log(f"sessions: {len(history_drift)} older session(s) drift (history — a session captures its model "
            f"at creation; not live work)")
    if over:
        log(f"sessions: {len(over)} live session(s) over the {threshold}-input-token threshold — recreate one "
            f"with: scripts/new-session.sh <agent> <room>")

    if not mismatches:
        log(f"sessions: ok — no live session drifts from its agent's model"
            + (f"; {len(over)} over the token threshold (a flag, not a failure)" if over else ""))
        return 0

    if not do_fix:
        log(f"sessions: {len(mismatches)} live session(s) drift from their agent's model; re-run with --fix to switch them")
        return 1

    switched = 0
    for session, want in mismatches:
        sid = session.get("id")
        try:
            _, status = api_request(url, password, "POST", f"/api/session/{sid}/model", {"model": want})
        except Exception as e:  # noqa: BLE001 - keep going through the list
            log(f"sessions: FIX FAILED for {sid}: {e}")
            continue
        if status in (200, 204):
            log(f"sessions: switched {sid} -> {format_model(want)} (HTTP {status})")
            switched += 1
        else:
            log(f"sessions: FIX FAILED for {sid} (HTTP {status})")

    try:
        sessions2, agents2 = fetch_sessions_agents(url, password)
    except Exception as e:  # noqa: BLE001
        log(f"sessions: could not re-check after --fix: {e}")
        return 1
    live2, _ = split_live(sessions2, active_session_ids(url, password), minutes)
    remaining = model_mismatches(live2, agents2)
    if remaining:
        log(f"sessions: {len(remaining)} session(s) still drift after --fix")
        return 1
    log(f"sessions: fixed {switched} session(s); re-check clean")
    return 0


def _mock_server(agents, sessions):
    """A tiny in-process server with the real API shapes, for --selftest.

    Returns (httpd, url, password, state). Never touches the real service.
    """
    state = {"sessions": sessions}

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):  # silence the access log
            pass

        def _json(self, code, payload):
            body = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == "/api/agent":
                self._json(200, {"data": agents})
            elif self.path == "/api/session":
                self._json(200, {"data": state["sessions"]})
            elif self.path == "/api/session/active":
                # No session is "active" in the fixture: the recency rule
                # decides what is live on its own.
                self._json(200, {"data": {}})
            else:
                self._json(404, {})

        def do_POST(self):
            m = re.match(r"^/api/session/([^/]+)/model$", self.path)
            if not m:
                self._json(404, {})
                return
            length = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(length) or b"{}")
            for s in state["sessions"]:
                if s.get("id") == m.group(1):
                    s["model"] = body.get("model")
            self.send_response(204)
            self.end_headers()

    httpd = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, f"http://127.0.0.1:{httpd.server_address[1]}", "fixture-not-a-secret", state


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

    # Session-model drift (row #104) and the live/history split + the token
    # threshold (row #109): the comparison logic against fixtures.
    now_ms = int(time.time() * 1000)
    agents = [
        {"id": "openscanner/recon",
         "model": {"id": "qwen3.8-max", "providerID": "opencode-go", "variant": "medium"}},
        {"id": "openscanner/verifier",
         "model": {"id": "qwen3.7-plus", "providerID": "opencode-go", "variant": "max"}},
    ]
    sessions = [
        # live: a real drift (its own model differs from the agent's)
        {"id": "ses_recon1", "agent": "openscanner/recon", "title": "recon-1",
         "model": {"id": "deepseek-v4.1-flash", "providerID": "opencode", "variant": "max"},
         "time": {"idle": now_ms - 60 * 1000, "updated": now_ms}},
        # live: the agent's model, nothing to report
        {"id": "ses_verify3", "agent": "openscanner/verifier", "title": "verify-3",
         "model": {"id": "qwen3.7-plus", "providerID": "opencode-go", "variant": "max"},
         "time": {"idle": now_ms - 60 * 1000, "updated": now_ms}},
        # live: no model of its own -> the agent's default, NOT drift
        {"id": "ses_default", "agent": "openscanner/verifier", "title": "verify-5",
         "model": None, "time": {"idle": now_ms - 60 * 1000, "updated": now_ms}},
        # live: over the token threshold
        {"id": "ses_over", "agent": "openscanner/recon", "title": "recon-9",
         "model": {"id": "qwen3.8-max", "providerID": "opencode-go", "variant": "medium"},
         "tokens": {"input": 5000000}, "time": {"idle": now_ms - 60 * 1000, "updated": now_ms}},
        # history: its record was touched a minute ago, but it has not worked
        # for an hour -> history, not live work
        {"id": "ses_history", "agent": "openscanner/verifier", "title": "old",
         "model": {"id": "stale", "providerID": "p", "variant": ""},
         "time": {"idle": now_ms - 3600 * 1000, "updated": now_ms}},
        # not ours
        {"id": "ses_foreign", "agent": "some/other", "title": "unrelated",
         "model": {"id": "whatever", "providerID": "p", "variant": ""},
         "time": {"idle": now_ms - 60 * 1000, "updated": now_ms}},
        {"id": "ses_archived", "agent": "openscanner/recon", "title": "old",
         "model": {"id": "stale", "providerID": "p", "variant": ""},
         "time": {"archived": 1, "idle": now_ms - 60 * 1000, "updated": now_ms}},
    ]
    drifted = [s["id"] for s, _ in model_mismatches(sessions, agents)]
    if drifted != ["ses_recon1", "ses_history"]:
        log(f"SELFTEST FAIL: model_mismatches -> {drifted}, expected ['ses_recon1', 'ses_history']")
        failures += 1
    else:
        log("SELFTEST ok: model drift detected on the matching sessions only")
    if format_model(agents[0]["model"]) != "opencode-go/qwen3.8-max#medium":
        log(f"SELFTEST FAIL: format_model -> {format_model(agents[0]['model'])}")
        failures += 1
    else:
        log("SELFTEST ok: model formatting")

    # Row #109: a null record model is the agent's default, not drift, and it
    # is rendered as such.
    if format_session_model(sessions[2]) != "default (agent)":
        log(f"SELFTEST FAIL: format_session_model -> {format_session_model(sessions[2])}")
        failures += 1
    else:
        log("SELFTEST ok: a null record model renders as 'default (agent)'")

    # Row #109: the live/history split. The active map wins; otherwise the
    # recency window decides — and the work marker is `time.idle`, not
    # `time.updated` (a record touched without work is not live work).
    live, history = split_live(sessions, {"ses_verify3"}, 30, now_ms=now_ms)
    live_ids = [s["id"] for s in live]
    if live_ids != ["ses_recon1", "ses_verify3", "ses_default", "ses_over", "ses_foreign"]:
        log(f"SELFTEST FAIL: split_live -> live {live_ids}")
        failures += 1
    elif [s["id"] for s in history] != ["ses_history", "ses_archived"]:
        log(f"SELFTEST FAIL: split_live -> history {[s['id'] for s in history]}")
        failures += 1
    else:
        log("SELFTEST ok: live = held by the server or worked recently; the rest is history")

    # Row #109: the threshold arithmetic. At the threshold is not over it.
    for value, threshold, expected_over in ((999999, 1000000, False), (1000000, 1000000, False),
                                             (1000001, 1000000, True), (0, 0, False), (1, 0, True)):
        if is_over_threshold(value, threshold) != expected_over:
            log(f"SELFTEST FAIL: is_over_threshold({value}, {threshold}) != {expected_over}")
            failures += 1
            break
    else:
        log("SELFTEST ok: the token threshold is exclusive")
    if session_input_tokens(sessions[3]) != 5000000 or session_input_tokens({}) != 0:
        log("SELFTEST FAIL: session_input_tokens")
        failures += 1
    else:
        log("SELFTEST ok: the input-token count is read (and defaults to 0)")

    # End-to-end report+fix against the mock server (catches -> switches -> clean).
    httpd, url, password, _ = _mock_server([dict(a) for a in agents], [dict(s) for s in sessions])
    original = globals()["local_server_url"]
    globals()["local_server_url"] = lambda: (url, password)
    try:
        report = check_sessions(do_fix=False)
        fixed = check_sessions(do_fix=True)
        after = check_sessions(do_fix=False)
        if (report, fixed, after) != (1, 0, 0):
            log(f"SELFTEST FAIL: mock report/fix/after -> {(report, fixed, after)}, expected (1, 0, 0)")
            failures += 1
        else:
            log("SELFTEST ok: session drift reported, switched, and re-checked clean")
    finally:
        globals()["local_server_url"] = original
        httpd.shutdown()
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
    if "--sessions" in argv or "--fix" in argv:
        sessions = check_sessions(do_fix=("--fix" in argv))
        if sessions == 1:
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
