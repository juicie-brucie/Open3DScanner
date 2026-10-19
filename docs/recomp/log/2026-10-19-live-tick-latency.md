- 2026-10-19 — the operator reports the UI's **data fps at 2–8** against a 10 Hz tick ceiling, worse when the scanner is moved
  by hand, and asks for measurement before any scan_server change, with an eventual operating margin above the device's
  frame rate. Filed: **#111** (instrument the four per-tick segments — pose, publish-getter, our post-processing+encode,
  broadcast — with no behavior change), **#112** (the hardware baseline, no-client vs UI-open), **#113** (the
  operating-margin goal, blocked on the baseline). Note the rate discrepancy recorded in #113: the capture measured 50 fps
  nominal / 51.2 Hz; the operator states 60 fps.
