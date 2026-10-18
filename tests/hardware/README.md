# Hardware tests

Scripts and programs that exercise the real scanner (CR-Scan Raptor Pro). They are
the bridge between the recovered contract and observed device behavior.

- Written by the `builder` agent, run by the external hardware agent (Claude Code).
- Each test should print a clear **observed vs. expected** line so the result can be
  compared against the header without re-deriving anything.

Results are written to `../results/`, one file per run, never edited in place.
- Rerun procedure for row #32's scale acceptance: [`plan32-scale-rerun.md`](./plan32-scale-rerun.md).
