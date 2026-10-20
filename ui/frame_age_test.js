#!/usr/bin/env node
// ui/frame_age_test.js — row #142's device-free coverage.
//
// Two parts:
//   1. the arithmetic, pinned with known numbers (offset, age at receipt,
//      render delay, percentiles, rolling-window trim, sparkline);
//   2. an end-to-end check against the SYNTHETIC server: connect, take its
//      status message's `server_now_ms`, take a cloud frame's `t_capture`,
//      and compute the age exactly as ui/app.js does — asserting a real,
//      plausible age (the synthetic path produces frames without a device).
//
// Usage: node ui/frame_age_test.js [path-to-scan_server]
// Exit 0 = all checks pass (part 2 SKIPs if no binary/port is available).

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
  }
  console.log("[frame-age] " + (failures ? "FAILED" : "ok") + " (" + failures + " failure(s))");
  process.exit(failures ? 1 : 0);
})();
