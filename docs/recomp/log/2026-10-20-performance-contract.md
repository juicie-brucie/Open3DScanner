- 2026-10-20 — **the standard changed from "fast enough" to "as fast as possible"** (operator): the ingest is the start of a long
  pipeline — tracking, marker ID, ellipse transforms, exposure — and the product's ability to extend Creality Scan and run on
  **lower-end hardware** depends on it. **#152** files the performance contract (reference profile, per-stage budgets, a regression
  benchmark, a priced waste inventory); **#153** files the first concrete waste: the exposure/laser config is re-asserted **every
  tick** (`main.cpp:1027`/`:1130`) although the value rarely changes — one of the two calls that dominate the live tick's unsegmented
  ~5–6 ms.
