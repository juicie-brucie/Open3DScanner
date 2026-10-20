// Frame-age arithmetic for the HUD (row #142).
//
// Two clocks are involved and must never be conflated:
//
//   * the SERVER's wall clock (epoch ms). The V1 envelope's `t_capture` and
//     `t_server` are on it (tools/scan_server/main.cpp, NowMs()), and every
//     status message carries `server_now_ms` from the same clock.
//   * the PAGE's monotonic clock (performance.now()). Used ONLY for the local
//     render delay, so a wall-clock jump cannot corrupt a delta.
//
//   age at receipt = (local wall at receipt + server clock offset) - t_capture
//   frame age      = age at receipt + (render perf - receipt perf)
//
// The offset is estimated from the status message, so it carries that
// message's one-way transit (sub-millisecond on loopback): the age is an
// estimate, not a measurement. What it does *not* carry is the device-capture
// leg — `t_capture` is the server tick's read time (see the row #136 map), so
// this is the tick -> render age, and the HUD label says so.
//
// Pure: no DOM, no timers. Loaded by ui/index.html and by ui/frame_age_test.js
// under Node.
const FrameAge = (function () {
  "use strict";

  const DEFAULT_WINDOW_MS = 60000;

  // Nearest-rank percentile, matching the server's perf_window.h so the HUD
  // and the [perf] lines agree on what "p95" means.
  function percentile(sorted, p) {
    if (!sorted.length) return 0;
    let index = Math.ceil((p * sorted.length) / 100) - 1;
    if (index < 0) index = 0;
    if (index >= sorted.length) index = sorted.length - 1;
    return sorted[index];
  }

  class Tracker {
    constructor(windowMs) {
      this.windowMs = windowMs > 0 ? windowMs : DEFAULT_WINDOW_MS;
      this.offsetMs = null; // server wall clock - local wall clock
      this.samples = []; // { t, age } in page monotonic ms
    }

    // Called on every status message. Returns the offset it now uses, or null
    // when the message carries no usable clock (older server builds).
    noteServerClock(serverNowMs, localWallMs) {
      if (!Number.isFinite(serverNowMs) || !Number.isFinite(localWallMs)) {
        return this.offsetMs;
      }
      this.offsetMs = serverNowMs - localWallMs;
      return this.offsetMs;
    }

    // Called when a cloud frame arrives. Returns its age at receipt in ms, or
    // null while the server clock is still unknown.
    ageAtReceipt(tCaptureMs, localWallMs) {
      if (this.offsetMs === null) return null;
      if (!Number.isFinite(tCaptureMs) || !Number.isFinite(localWallMs)) return null;
      return localWallMs + this.offsetMs - tCaptureMs;
    }

    // Called when that same frame is actually rendered. Returns the frame age
    // (receipt age + local render delay), or null if the age is unknown.
    noteRendered(ageAtReceiptMs, receiptPerfMs, renderPerfMs) {
      if (ageAtReceiptMs === null || !Number.isFinite(ageAtReceiptMs)) return null;
      if (!Number.isFinite(receiptPerfMs) || !Number.isFinite(renderPerfMs)) return null;
      const delay = Math.max(0, renderPerfMs - receiptPerfMs);
      const age = ageAtReceiptMs + delay;
      this.samples.push({ t: renderPerfMs, age: age });
      this._trim(renderPerfMs);
      return age;
    }

    _trim(nowPerfMs) {
      const cutoff = nowPerfMs - this.windowMs;
      let drop = 0;
      while (drop < this.samples.length && this.samples[drop].t < cutoff) drop++;
      if (drop > 0) this.samples.splice(0, drop);
    }

    // Stats over the retained (rolling) window.
    stats() {
      if (!this.samples.length) return { n: 0, p50: 0, p95: 0, min: 0, max: 0 };
      const sorted = this.samples.map((s) => s.age).sort((a, b) => a - b);
      return {
        n: sorted.length,
        p50: percentile(sorted, 50),
        p95: percentile(sorted, 95),
        min: sorted[0],
        max: sorted[sorted.length - 1],
      };
    }

    series() {
      return this.samples.slice();
    }

    reset() {
      this.samples.length = 0;
    }
  }

  // A polyline for the rolling line: oldest sample at x=0, newest at x=width;
  // y=height at age 0 and y=0 at `ceilingMs`.
  function sparkPoints(samples, nowPerfMs, windowMs, width, height, ceilingMs) {
    if (!samples.length) return "";
    const span = windowMs > 0 ? windowMs : DEFAULT_WINDOW_MS;
    const ceiling = ceilingMs > 0 ? ceilingMs : 1;
    const pts = [];
    for (const s of samples) {
      const age = Math.max(0, s.age);
      const x = (1 - (nowPerfMs - s.t) / span) * width;
      if (x < 0) continue;
      const y = height - Math.min(age / ceiling, 1) * height;
      pts.push(x.toFixed(1) + "," + y.toFixed(1));
    }
    return pts.join(" ");
  }

  // The one-line HUD summary. Kept here so the arithmetic test can pin the
  // exact text the operator reads.
  function describe(stats) {
    if (!stats || !stats.n) return "-- (waiting for the server clock)";
    return "p50 " + stats.p50.toFixed(1) + " / p95 " + stats.p95.toFixed(1) + " ms";
  }

  return {
    Tracker: Tracker,
    percentile: percentile,
    sparkPoints: sparkPoints,
    describe: describe,
    DEFAULT_WINDOW_MS: DEFAULT_WINDOW_MS,
  };
})();

// Node (the arithmetic test) — the browser gets the global above.
if (typeof module !== "undefined" && module.exports) module.exports = FrameAge;
