// Coverage voxel sets for the HUD (row #76; the update trigger is row #160).
//
// Pure: no DOM, no timers. Loaded by ui/index.html and by ui/coverage_test.js
// under Node, the same shape as ui/frame_age.js.
//
// Two pointers, not copies:
//   * docs/recon/findings/scan-live-data.md §17.2 — why the pose channel is
//     not the live scanner motion, which is what row #160's frozen counters
//     came down to;
//   * docs/recomp/PLAN.md row #160 — the observation the fix is measured by.
const Coverage = (function () {
  "use strict";

  // The red overlay goes over the wire (one float triple per point), so its
  // geometry stays bounded. The HUD's count is the true total — before #160
  // the two were the same number, so the count could not move while the
  // under-covered set was larger than the budget.
  const DEFAULT_BUDGET = 20000; // floats, not points

  function voxelKey(x, y, z, cell) {
    return Math.round(x / cell) + "," + Math.round(y / cell) + "," + Math.round(z / cell);
  }

  function quantize(raw) {
    return Math.pow(2, Math.ceil(Math.log2(raw)));
  }

  // Maps per-voxel hit count → RGB colour on a red→orange→green→blue gradient.
  // log2 scale saturating at 256 hits: 1 hit = red, ~8 = orange, ~64 = green,
  // 256+ = blue. Scale-independent: works regardless of voxel size or tick rate.
  function densityColor(count) {
    const t = Math.min(1.0, Math.log2(count + 1) / 8.0);
    const stops = [
      [1.00, 0.15, 0.10],  // red   (sparse / just started)
      [1.00, 0.55, 0.00],  // orange
      [0.10, 0.85, 0.30],  // green
      [0.15, 0.55, 1.00],  // blue  (dense / well covered)
    ];
    const scaled = t * 3;
    const lo = Math.min(Math.floor(scaled), 2);
    const s = scaled - lo;
    const a = stops[lo], b = stops[lo + 1];
    return [a[0] + s * (b[0] - a[0]), a[1] + s * (b[1] - a[1]), a[2] + s * (b[2] - a[2])];
  }

  class Tracker {
    constructor(budget) {
      this.budget = budget > 0 ? budget : DEFAULT_BUDGET;
      this.cell = null;
      this.visited = new Set();
      this.seen = new Map(); // key → hit count (drives per-point density colours)
      this.underCount = 0;
      this.underPositions = new Float32Array(0);
      this.resets = 0;
    }

    // One grid for the whole session, so a key from one frame means the same
    // voxel in the next. Quantized to powers of two with factor-of-two
    // hysteresis: the scene growing a little must not re-anchor the grid.
    // A genuine scale change clears the sets — keys from two grids are not
    // comparable, and comparing them was part of #160 (seen climbed while the
    // covered fraction could not).
    scale(radiusHint) {
      const radius = Number.isFinite(radiusHint) && radiusHint > 0 ? radiusHint : 1;
      const raw = Math.max(radius / 20, 1e-4);
      if (this.cell === null) {
        this.cell = quantize(raw);
        return this.cell;
      }
      if (raw >= this.cell * 2 || raw <= this.cell / 2) {
        this.cell = quantize(raw);
        this.visited.clear();
        this.seen.clear();
        this.underCount = 0;
        this.underPositions = new Float32Array(0);
        this.resets++;
      }
      return this.cell;
    }

    markVisited(x, y, z) {
      if (this.cell === null) return 0;
      const cell = this.cell;
      const r = 2; // ~2 cells of scanner proximity counts as visited
      const cx = Math.round(x / cell), cy = Math.round(y / cell), cz = Math.round(z / cell);
      for (let i = -r; i <= r; i++) {
        for (let j = -r; j <= r; j++) {
          for (let k = -r; k <= r; k++) {
            this.visited.add((cx + i) + "," + (cy + j) + "," + (cz + k));
          }
        }
      }
      return this.visited.size;
    }

    // A pose message. Kept for replay/registration sessions, where the pose
    // does move; it is no longer the only trigger (see observe()).
    observePose(x, y, z, radiusHint) {
      this.scale(radiusHint);
      return this.markVisited(x, y, z);
    }

    // A cloud snapshot. Accumulates per-voxel hit counts, builds a per-point
    // density colour buffer (red→orange→green→blue), maintains the under-covered
    // overlay for the HUD count, and marks the newly observed patch as visited.
    observe(positions, count, radiusHint) {
      const cell = this.scale(radiusHint);
      const under = [];
      let underTotal = 0, newPoints = 0, nx = 0, ny = 0, nz = 0;
      const densityColors = new Float32Array(count * 3);
      for (let i = 0; i < count; i++) {
        const x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
        const key = voxelKey(x, y, z, cell);
        const prev = this.seen.get(key) || 0;
        this.seen.set(key, prev + 1);
        if (prev === 0) {
          newPoints++;
          nx += x; ny += y; nz += z;
        }
        if (!this.visited.has(key)) {
          underTotal++;
          if (under.length < this.budget) under.push(x, y, z);
        }
        const c = densityColor(prev + 1);
        densityColors[i * 3] = c[0];
        densityColors[i * 3 + 1] = c[1];
        densityColors[i * 3 + 2] = c[2];
      }
      this.underCount = underTotal;
      this.underPositions = new Float32Array(under);
      if (newPoints > 0) this.markVisited(nx / newPoints, ny / newPoints, nz / newPoints);
      return { newPoints, underCount: underTotal, densityColors };
    }

    // What the renderer may draw from underPositions — the bounded overlay,
    // not the HUD's true total.
    overlayPointCount() {
      return this.underPositions.length / 3;
    }

    percentCovered() {
      if (this.seen.size === 0) return 0;
      let visited = 0;
      for (const key of this.seen.keys()) if (this.visited.has(key)) visited++;
      return (100 * visited) / this.seen.size;
    }

    // window.openScannerCoverage()'s shape, plus the grid diagnostics the
    // hardware re-observation needs when a counter still looks stuck.
    counters() {
      return {
        visited: this.visited.size,
        seen: this.seen.size,
        underCoveredPoints: this.underCount,
        cell: this.cell,
        resets: this.resets,
      };
    }
  }

  return { Tracker, voxelKey, DEFAULT_BUDGET };
})();

// Node (the arithmetic test) — the browser gets the global above.
if (typeof module !== "undefined" && module.exports) module.exports = Coverage;
