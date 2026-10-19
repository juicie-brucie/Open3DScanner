- 2026-10-19 — `SPEC-TARGETS.md` added: the Creality Raptor Pro's **advertised** rates (blue 22-line 660k pts/s and
  7-line 420k pts/s, both up to 60 FPS; NIR 3.58M measurements/s up to 30 FPS) and host requirements, next to what the
  repo has actually measured. The key correction it records: the captured session **requested 50 fps** on IR and depth
  (`[6,9,1920,1200,50]` / `[7,9,1920,1200,50]`) and polled at ~51.2 Hz — so 51.2 is the configured profile, not an M1
  ceiling, and no 60 fps observation exists yet. Filed **#114** (enumerate profiles per interface; is a 60 fps profile
  offered, and which one does each mode use?) and **#115** (cross-machine rate check on the M1, the M2 Air and the
  Windows 5900X/2080 Ti host).
