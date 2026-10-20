#!/usr/bin/env node
// ui/coverage_test.js — row #160's device-free check for the coverage update
// trigger, plus the row #76 invariants it must keep (bounded overlay, the
// percentage, the counters window.openScannerCoverage() reports).
//
// Usage: node ui/coverage_test.js
// Exit 0 = all checks pass. No device, no server, no DOM.

"use strict";

const path = require("path");
const Coverage = require(path.join(__dirname, "coverage.js"));

let failures = 0;
function check(name, fn) {
  try {
    fn();
    console.log("[coverage] " + name.padEnd(64) + "PASS");
  } catch (e) {
    failures++;
    console.log("[coverage] " + name.padEnd(64) + "FAIL: " + e.message);
  }
}

// A patch of points around (x, y, z), `n` per axis, `spacing` apart.
function patch(x, y, z, n, spacing) {
  const out = [];
  for (let i = 0; i < n; i++) {
    for (let j = 0; j < n; j++) {
      for (let k = 0; k < n; k++) {
        out.push(x + i * spacing, y + j * spacing, z + k * spacing);
      }
    }
  }
  return new Float32Array(out);
}

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

// --- the grid ---------------------------------------------------------------

check("voxelKey rounds to the grid cell", () => {
  assert(Coverage.voxelKey(0.049, 0.011, -0.049, 0.05) === "1,0,-1", "rounding");
  assert(Coverage.voxelKey(0, 0, 0, 0.05) === "0,0,0", "origin");
});

check("the grid is stable while the scene grows a little", () => {
  const t = new Coverage.Tracker();
  t.observe(patch(0, 0, 0, 4, 0.01), 64, 0.2);
  const cell = t.cell;
  t.observe(patch(0.05, 0, 0, 4, 0.01), 64, 0.25); // radius +25%
  assert(t.cell === cell, "cell re-anchored on a small growth: " + cell + " -> " + t.cell);
  assert(t.resets === 0, "unexpected grid reset");
});

check("a real scale change clears the sets rather than mixing grids", () => {
  const t = new Coverage.Tracker();
  t.observe(patch(0, 0, 0, 4, 0.01), 64, 0.2);
  const seenBefore = t.seen.size;
  assert(seenBefore > 0, "setup: no seen voxels");
  t.observe(patch(0, 0, 0, 4, 0.01), 64, 10); // radius 50x -> new grid
  assert(t.cell !== null, "cell should be set");
  assert(t.resets === 1, "expected one reset, got " + t.resets);
  // The second observe repopulates on the new grid; nothing from the old one
  // survives, and the counts are self-consistent (seen is never 0 after a
  // frame).
  assert(t.seen.size > 0, "seen empty after the reset frame");
  assert(t.visited.size === 125, "visited should be one box on the new grid, got " + t.visited.size);
});

// --- the row #160 defect ----------------------------------------------------

check("static pose: new frames still move visited and the counters", () => {
  const t = new Coverage.Tracker();
  const radius = 0.2;
  // A pose that never changes, as in the #76 live observation.
  t.observePose(0, 0, 0, radius);
  const first = t.counters();
  assert(first.visited === 125, "one markVisited box = 125 voxels, got " + first.visited);

  // Three patches at different places, as the operator walks the scanner.
  for (let i = 1; i <= 3; i++) {
    t.observe(patch(i * 0.1, 0, 0, 4, 0.01), 64, radius);
  }
  const later = t.counters();
  assert(later.seen > first.seen, "seen must climb (it did before the fix)");
  assert(later.visited > first.visited,
    "visited froze at " + first.visited + " — the #160 defect is back");
  assert(later.underCoveredPoints !== first.underCoveredPoints ||
    later.seen !== first.seen, "counters must respond");
});

check("the covered fraction rises as new patches arrive", () => {
  const t = new Coverage.Tracker();
  const radius = 0.2;
  t.observe(patch(0, 0, 0, 4, 0.01), 64, radius);
  const p0 = t.percentCovered();
  for (let i = 1; i <= 6; i++) t.observe(patch(i * 0.01, 0, 0, 4, 0.01), 64, radius);
  const p1 = t.percentCovered();
  assert(p1 >= p0, "percentage went backwards: " + p0 + " -> " + p1);
  assert(p1 > 0, "percentage is still 0 — a standoff/blind trigger");
});

check("a moving pose still marks visited (replay / registration sessions)", () => {
  const t = new Coverage.Tracker();
  t.observePose(0, 0, 0, 0.2);
  const a = t.visited.size;
  t.observePose(0.5, 0, 0, 0.2);
  assert(t.visited.size > a, "pose move did not extend visited");
});

// --- the rendering invariants ----------------------------------------------

check("the HUD count is the true total, not the overlay's cap", () => {
  const t = new Coverage.Tracker(20000); // floats
  // 30,000 under-covered points, all in distinct voxels.
  const positions = new Float32Array(3 * 30000);
  for (let i = 0; i < 30000; i++) {
    positions[i * 3] = i * 0.5;       // 0.5 m apart -> distinct voxels
    positions[i * 3 + 1] = 0;
    positions[i * 3 + 2] = 0;
  }
  t.observe(positions, 30000, 100);   // radius 100 -> cell 8 m, keys differ
  assert(t.underCount === 30000, "underCoveredPoints must be the true total, got " + t.underCount);
  assert(t.underPositions.length <= 20001,
    "the overlay must stay bounded, got " + t.underPositions.length + " floats");
  assert(t.underPositions.length % 3 === 0, "overlay must be whole triples");
  assert(t.overlayPointCount() === t.underPositions.length / 3,
    "the renderer's draw count must be the bounded overlay's");
});

check("points already visited are not on the red overlay", () => {
  const t = new Coverage.Tracker();
  const radius = 0.2;
  const p = patch(0, 0, 0, 4, 0.01);
  t.observe(p, 64, radius);          // marks the patch's centre visited
  const after = t.observe(p, 64, radius);
  assert(after.underCount < 64, "the same patch is still fully under-covered: " + after.underCount);
});

check("counters() keeps the hook's recorded shape", () => {
  const t = new Coverage.Tracker();
  t.observe(patch(0, 0, 0, 3, 0.01), 27, 0.2);
  const c = t.counters();
  for (const k of ["visited", "seen", "underCoveredPoints"]) {
    assert(typeof c[k] === "number" && Number.isFinite(c[k]), "missing counter " + k);
  }
});

check("an empty tracker reports no coverage rather than dividing by zero", () => {
  const t = new Coverage.Tracker();
  assert(t.percentCovered() === 0, "empty percentCovered");
  const c = t.counters();
  assert(c.seen === 0 && c.visited === 0 && c.underCoveredPoints === 0, "empty counters");
});

console.log("[coverage] " + (failures ? "FAILED" : "ok") + " (" + failures + " failure(s))");
process.exit(failures ? 1 : 0);
