- 2026-10-20 — **#155 (the full-pipeline stage map) and #156 (the full-frame benchmark) filed**, per the operator: the map must carry
  **time** as well as volume (extending #136's data-flow map), and the benchmark must be **one frame through every stage** with a total
  latency, not per-surface profiles. They are the end-to-end counterpart to #152's per-stage budgets and #147's surface measurements.
