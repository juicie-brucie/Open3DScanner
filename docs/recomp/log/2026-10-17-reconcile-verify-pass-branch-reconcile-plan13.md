- 2026-10-17 — reconcile (verify pass, branch `reconcile/plan13-row-corrections`):
  verify-1's read-only pass corrected five rows. **#13(b) retracted** — the
  "upstream of `send_device_msg`" claim was wrong; the header's "shortly after
  returning from `onMessage`" ordering is *corroborated* (`raw.log:75-78`), and
  only the mechanism reading changes (concurrent watcher-thread fault, not a
  post-return continuation). **#14** main claim holds; thread relabelled
  NetDeviceWatcher/GVCP (its `std::function`-indirection point stands — that was
  §9's blind spot). **#15** timestamps verify but causality was wrong: the
  create→destroy→create burst *precedes* check 7 and tracks `remove()` racing the
  watcher, so **#11's re-run should target the remove-side race, not the
  re-add**. **#16** reframed: not a rule violation but a gap in the header rule
  at the destroy boundary, narrower severity (`688 → 693`), and the #13/#14 crash
  dropped as non-probative. **#17** holds; added that `raw.log:75`'s hash equals
  the registration hash, so no watcher-thread delivery ever occurred. Also
  relabelled the thread in **#11** and corrected the operator-authored
  crash-analysis file in place (labelled); run records untouched.
