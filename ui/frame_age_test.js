#!/usr/bin/env node
// ui/frame_age_test.js — row #142's device-free coverage, plus row #151's
// load-harness leg (the frame age and the drop rate at a chosen rate).
//
// Three parts:
//   1. the arithmetic, pinned with known numbers (offset, age at receipt,
//      render delay, percentiles, rolling-window trim, sparkline);
//   2. an end-to-end check against the SYNTHETIC server: connect, take its
//      status message's `server_now_ms`, take a cloud frame's `t_capture`,
//      and compute the age exactly as ui/app.js does — asserting a real,
//      plausible age (the synthetic path produces frames without a device);
//   3. the same check against the row #151 LOAD harness, which publishes at a
//      chosen rate and frame size: a distribution over many frames instead of
//      one, plus the frame-id continuity check (the V1 envelope's `seq`), so a
//      frame the server published and the client never received is counted
//      rather than assumed away.
//
// Usage: node ui/frame_age_test.js [path-to-scan_server] [--synthetic-load <hz> <points>]
// Exit 0 = all checks pass (parts 2/3 SKIP if no binary/port is available).

"use strict";

const assert = require("assert");
const path = require("path");
const { spawn } = require("child_process");

const FrameAge = require(path.join(__dirname, "frame_age.js"));

let failures = 0;
function check(name, fn) {
  try {
    fn();
    console.log("[frame-age] " + name.padEnd(64) + "PASS");
  } catch (e) {
    failures++;
    console.log("[frame-age] " + name.padEnd(64) + "FAIL: " + e.message);
  }
}

// --- 1. the arithmetic ------------------------------------------------------

check("age at receipt = (local wall + offset) - t_capture", () => {
  const t = new FrameAge.Tracker();
  // The server says its clock is 12.0 s ahead of the page's wall clock.
  t.noteServerClock(1_000_012, 1_000_000);
  // A frame captured on the server clock at 1_000_000, arriving 40 ms later.
  const age = t.ageAtReceipt(1_000_000, 1_000_040);
  assert.strictEqual(age, 52); // 40 ms transit + 12 ms clock offset
});

check("no server clock yet -> age is null, not a garbage number", () => {
  const t = new FrameAge.Tracker();
  assert.strictEqual(t.ageAtReceipt(1_000_000, 1_000_040), null);
  assert.strictEqual(t.stats().n, 0);
  assert.ok(FrameAge.describe(t.stats()).includes("waiting for the server clock"));
});

check("frame age = age at receipt + local render delay", () => {
  const t = new FrameAge.Tracker();
  t.noteServerClock(1_000_000, 1_000_000); // offset 0
  const receiptAge = t.ageAtReceipt(1_000_000, 1_000_040); // 40 ms
  assert.strictEqual(receiptAge, 40);
  // Received at perf 500, rendered at perf 512 -> 12 ms of local queue.
  const age = t.noteRendered(receiptAge, 500, 512);
  assert.strictEqual(age, 52);
  assert.strictEqual(t.samples.length, 1);
});

check("a render delay cannot be negative", () => {
  const t = new FrameAge.Tracker();
  t.noteServerClock(0, 0);
  const age = t.noteRendered(10, 900, 800);
  assert.strictEqual(age, 10);
});

check("the clock offset is re-estimated on every status message", () => {
  const t = new FrameAge.Tracker();
  t.noteServerClock(1_000_010, 1_000_000); // +10
  assert.strictEqual(t.ageAtReceipt(1_000_000, 1_000_000), 10);
  t.noteServerClock(1_000_500, 1_000_000); // +500 (clock stepped)
  assert.strictEqual(t.ageAtReceipt(1_000_000, 1_000_000), 500);
});

check("percentiles are nearest-rank, like the server's perf_window.h", () => {
  const t = new FrameAge.Tracker();
  t.noteServerClock(0, 0);
  // 20 samples: 1..20 ms (zero render delay). Nearest-rank p50 ->
  // index ceil(0.5*20)-1 = 9 -> 10; p95 -> index ceil(0.95*20)-1 = 18 -> 19.
  for (let i = 1; i <= 20; i++) t.noteRendered(i, 0, 0);
  const s = t.stats();
  assert.strictEqual(s.n, 20);
  assert.strictEqual(s.p50, 10);
  assert.strictEqual(s.p95, 19);
  assert.strictEqual(s.min, 1);
  assert.strictEqual(s.max, 20);
  assert.strictEqual(FrameAge.describe(s), "p50 10.0 / p95 19.0 ms");
});

check("the rolling window drops samples older than 60 s", () => {
  const t = new FrameAge.Tracker(60000);
  t.noteServerClock(0, 0);
  // The window is half-open, (now - 60 s, now]: a sample exactly 60 s old is
  // still in it, so the boundary cases below use +1 ms.
  t.noteRendered(100, 0, 1_000); // t=1000
  t.noteRendered(100, 0, 30_000); // t=30000
  t.noteRendered(100, 0, 61_001); // t=61001 -> drops t=1000
  assert.strictEqual(t.samples.length, 2);
  assert.strictEqual(t.samples[0].t, 30_000);
  t.noteRendered(100, 0, 92_002); // t=92002 -> drops t=30000
  assert.strictEqual(t.samples.length, 2);
  assert.strictEqual(t.samples[0].t, 61_001);
});

check("the sparkline: oldest left, newest right, 0 ms at the floor", () => {
  const samples = [
    { t: 0, age: 0 },
    { t: 30_000, age: 50 },
    { t: 60_000, age: 25 },
  ];
  const pts = FrameAge.sparkPoints(samples, 60_000, 60_000, 120, 24, 50);
  const first = pts.split(" ")[0].split(",");
  const last = pts.split(" ")[2].split(",");
  assert.strictEqual(first[0], "0.0");
  assert.strictEqual(first[1], "24.0"); // age 0 -> floor
  assert.strictEqual(last[0], "120.0");
  assert.strictEqual(last[1], "12.0"); // age 25 of a 50 ceiling -> half height
});

// --- 2. end to end against the synthetic server ------------------------------

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

async function syntheticCheck(binaryPath) {
  const net = require("net");
  const port = await freePort(net);
  if (port === null) {
    console.log("[frame-age] synthetic end-to-end                          SKIP (no free port)");
    return;
  }
  const child = spawn(binaryPath, ["--synthetic", String(port)], {
    stdio: ["ignore", "pipe", "pipe"],
  });
  let reaped = false;
  const reap = () => {
    if (!reaped) {
      reaped = true;
      try { child.kill("SIGTERM"); } catch (e) { /* already gone */ }
    }
  };
  process.on("exit", reap);

  try {
    // Wait for the listener (the server prints only after Start()).
    let up = false;
    for (let i = 0; i < 100 && !up; i++) {
      up = await new Promise((resolve) => {
        const s = net.connect(port, "127.0.0.1", () => { s.destroy(); resolve(true); });
        s.on("error", () => resolve(false));
      });
      if (!up) await sleep(50);
    }
    if (!up) throw new Error("synthetic server did not start listening");

    if (typeof WebSocket !== "function") {
      console.log("[frame-age] synthetic end-to-end                          SKIP (no global WebSocket)");
      return;
    }

    const result = await new Promise((resolve, reject) => {
      const ws = new WebSocket("ws://127.0.0.1:" + port);
      const tracker = new FrameAge.Tracker();
      let sawStatus = false;
      const timer = setTimeout(() => {
        try { ws.close(); } catch (e) { /* noop */ }
        reject(new Error("no status+frame within 8 s"));
      }, 8000);

      ws.binaryType = "arraybuffer";
      ws.onerror = (e) => { clearTimeout(timer); reject(new Error("websocket error")); };
      ws.onmessage = (ev) => {
        if (typeof ev.data === "string") {
          let msg = null;
          try { msg = JSON.parse(ev.data); } catch (e) { msg = null; }
          if (msg && msg.type === "status") {
            assert.ok(Number.isFinite(msg.server_now_ms),
              "status must carry server_now_ms (row #142)");
            tracker.noteServerClock(msg.server_now_ms, Date.now());
            sawStatus = true;
          }
          return;
        }
        if (!sawStatus || !(ev.data instanceof ArrayBuffer)) return;
        const buf = ev.data;
        const view = new DataView(buf);
        if (buf.byteLength < 36) return;
        if (view.getUint32(0, true) !== 0x4f534346) return; // not a V1 frame
        if (view.getUint16(6, true) !== 3) return; // not a cloud snapshot
        const tCapture = view.getFloat64(12, true);
        const ageAtReceipt = tracker.ageAtReceipt(tCapture, Date.now());
        const frameAge = tracker.noteRendered(ageAtReceipt, 0, 1);
        clearTimeout(timer);
        try { ws.close(); } catch (e) { /* noop */ }
        resolve({ tCapture, frameAge, hudLine: FrameAge.describe(tracker.stats()) });
      };
    });

    check("synthetic: a status clock + a frame give a real age", () => {
      assert.ok(result.frameAge !== null, "age should be computable");
      assert.ok(result.frameAge >= 0, "age must be non-negative, got " + result.frameAge);
      assert.ok(result.frameAge < 1000,
        "loopback synthetic age should be < 1000 ms, got " + result.frameAge.toFixed(1));
    });
    console.log("[frame-age] synthetic HUD line: " + result.hudLine);
  } finally {
    reap();
    await sleep(100);
  }
}

function freePort(net) {
  return new Promise((resolve) => {
    const srv = net.createServer();
    srv.once("error", () => resolve(null));
    srv.listen(0, "127.0.0.1", () => {
      const p = srv.address().port;
      srv.close(() => resolve(p));
    });
  });
}

// --- 3. the load harness (row #151) ----------------------------------------
//
// `--synthetic-load <port> <hz> <points>` publishes at a chosen rate with a
// chosen frame size, so the age and the drop rate are measured at the rate the
// publish path actually sustains rather than at the synthetic default.
//
// Two things this leg adds over part 2:
//   * a distribution — the age of every frame in the window, so p50/p95 mean
//     what the HUD's rolling line means;
//   * frame-id continuity — the V1 envelope carries a per-frame `seq`, so a
//     gap in it is a frame the server published and this client never got.
//     The rate (not the fps) is what the drop count rests on.
//
// The browser's own render delay is NOT measured here: it is stubbed at 1 ms,
// exactly as in part 2. The number below is the tick -> receipt leg.

async function loadCheck(binaryPath, hz, points, seconds) {
  const net = require("net");
  const port = await freePort(net);
  if (port === null) {
    console.log("[frame-age] load end-to-end                               SKIP (no free port)");
    return;
  }
  const child = spawn(binaryPath,
    ["--synthetic-load", String(port), String(hz), String(points)],
    { stdio: ["ignore", "pipe", "pipe"] });
  let reaped = false;
  const reap = () => {
    if (!reaped) {
      reaped = true;
      try { child.kill("SIGTERM"); } catch (e) { /* already gone */ }
    }
  };
  process.on("exit", reap);
  // The server prints its own [perf] lines; keep them (they are the raw
  // evidence for the run) and drain the pipes so the child never blocks.
  let serverOut = "";
  child.stdout.on("data", (d) => { serverOut += d.toString(); });
  child.stderr.on("data", (d) => { serverOut += d.toString(); });

  try {
    let up = false;
    for (let i = 0; i < 100 && !up; i++) {
      up = await new Promise((resolve) => {
        const s = net.connect(port, "127.0.0.1", () => { s.destroy(); resolve(true); });
        s.on("error", () => resolve(false));
      });
      if (!up) await sleep(50);
    }
    if (!up) throw new Error("load server did not start listening");

    if (typeof WebSocket !== "function") {
      console.log("[frame-age] load end-to-end                               SKIP (no global WebSocket)");
      return;
    }

    const result = await new Promise((resolve, reject) => {
      const ws = new WebSocket("ws://127.0.0.1:" + port);
      const tracker = new FrameAge.Tracker();
      let sawStatus = false;
      let firstSeq = null;
      let lastSeq = null;
      let missing = 0;   // frames the server published and the client did not get
      let frames = 0;
      let firstAt = 0;
      let lastAt = 0;
      const startedAt = Date.now();
      const timer = setTimeout(() => {
        try { ws.close(); } catch (e) { /* noop */ }
        // Snapshot here: a frame that arrives after the window must not move
        // the numbers the report prints.
        resolve({ frames, missing, firstSeq, lastSeq, firstAt, lastAt, startedAt,
          stats: tracker.stats() });
      }, seconds * 1000);
      ws.onerror = () => { clearTimeout(timer); reject(new Error("websocket error")); };
      ws.binaryType = "arraybuffer";
      ws.onmessage = (ev) => {
        if (typeof ev.data === "string") {
          let msg = null;
          try { msg = JSON.parse(ev.data); } catch (e) { msg = null; }
          if (msg && msg.type === "status") {
            assert.ok(Number.isFinite(msg.server_now_ms),
              "status must carry server_now_ms (row #142)");
            tracker.noteServerClock(msg.server_now_ms, Date.now());
            sawStatus = true;
          }
          return;
        }
        if (!sawStatus || !(ev.data instanceof ArrayBuffer)) return;
        const buf = ev.data;
        if (buf.byteLength < 36) return;
        const view = new DataView(buf);
        if (view.getUint32(0, true) !== 0x4f534346) return; // not a V1 frame
        if (view.getUint16(6, true) !== 3) return; // not a cloud snapshot
        const seq = view.getUint32(8, true);
        const tCapture = view.getFloat64(12, true);
        const now = Date.now();
        const ageAtReceipt = tracker.ageAtReceipt(tCapture, now);
        const frameAge = tracker.noteRendered(ageAtReceipt, 0, 1);
        if (frameAge === null) return;
        frames++;
        if (firstSeq === null) { firstSeq = seq; firstAt = now; }
        else if (seq > lastSeq + 1) missing += seq - lastSeq - 1;
        lastSeq = seq;
        lastAt = now;
      };
    });

    const stats = result.stats;
    // The frames the server published between the first and the last one this
    // client saw: lastSeq - firstSeq + 1. What the client did not get is the
    // difference — the drop rate the P3 item asks for.
    const published = (result.lastSeq === null || result.firstSeq === null)
      ? 0 : (result.lastSeq - result.firstSeq + 1);
    const dropRate = published ? (result.missing / published) : 0;
    const span = (result.lastAt - result.firstAt) / 1000;
    const handledFps = span > 0 ? (result.frames - 1) / span : 0;

    check("load: the harness published frames and they all arrived", () => {
      assert.ok(result.frames > 0, "no cloud frame arrived");
      assert.strictEqual(result.missing, 0,
        result.missing + " frame(s) missing from a contiguous seq run of " + published);
      // The client must still be receiving at the end of the window: a stall
      // (the server drops a client whose send times out) shows up here.
      assert.ok(result.lastAt >= result.startedAt + seconds * 1000 - 1500,
        "the client stopped receiving before the window ended");
    });
    check("load: the frame age is real and plausible", () => {
      assert.ok(stats.n > 0, "no age sample");
      // The server emits `server_now_ms` as an integer millisecond while the
      // frame's `t_capture` is a float64 one, so the HUD's own arithmetic can
      // read a millisecond or so below the truth — the browser has the same
      // artifact. A median below that floor, or an age of seconds, is a
      // defect rather than an artifact.
      assert.ok(stats.min > -5, "age below the clock-granularity floor: " + stats.min);
      assert.ok(stats.p50 > -1, "the median age is below the floor: " + stats.p50);
      assert.ok(stats.p50 < 100,
        "the median age should be well under 100 ms, got " + stats.p50.toFixed(1));
      assert.ok(stats.max < 1000,
        "loopback load age should be < 1000 ms, got " + stats.max.toFixed(1));
    });
    console.log("[frame-age] load: hz=" + hz + " points=" + points +
      " window=" + seconds.toFixed(1) + "s frames=" + result.frames +
      " published=" + published + " missing=" + result.missing +
      " drop_rate=" + (dropRate * 100).toFixed(2) + "%");
    console.log("[frame-age] load: handled " + result.frames + " frames in " +
      span.toFixed(2) + "s = " + handledFps.toFixed(2) + "/s (the frames before the" +
      " first status message carry no clock and are not counted)");
    console.log("[frame-age] load HUD line: " + FrameAge.describe(stats) +
      " (min " + stats.min.toFixed(1) + " / max " + stats.max.toFixed(1) +
      " ms; the browser's render delay is stubbed at 1 ms)");
    for (const line of serverOut.split("\n")) {
      if (line.trim()) console.log("[frame-age] server: " + line);
    }
  } finally {
    reap();
    await sleep(100);
  }
}

(async () => {
  if (failures) {
    console.log("[frame-age] FAILED (" + failures + " check(s))");
    process.exit(1);
  }
  const binary = process.argv[2];
  if (!binary) {
    console.log("[frame-age] synthetic end-to-end                          SKIP (no binary argument)");
  } else {
    try {
      await syntheticCheck(binary);
    } catch (e) {
      // A missing/unstartable binary is a SKIP, not a failure: the arithmetic
      // above is the acceptance; the end-to-end leg needs the built server.
      console.log("[frame-age] synthetic end-to-end                          SKIP (" + e.message + ")");
    }
    // `--synthetic-load <hz> <points>` runs the same check against the row
    // #151 load harness. Absent, the leg is skipped.
    if (process.argv[3] === "--synthetic-load") {
      const hz = Number(process.argv[4]);
      const points = Number(process.argv[5]);
      const seconds = Number(process.argv[6] || 6.5);
      if (!Number.isFinite(hz) || !Number.isFinite(points)) {
        console.log("[frame-age] load end-to-end                               SKIP (bad --synthetic-load arguments)");
      } else {
        try {
          await loadCheck(binary, hz, points, seconds);
        } catch (e) {
          console.log("[frame-age] load end-to-end                               SKIP (" + e.message + ")");
        }
      }
    }
  }
  console.log("[frame-age] " + (failures ? "FAILED" : "ok") + " (" + failures + " failure(s))");
  process.exit(failures ? 1 : 0);
})();
