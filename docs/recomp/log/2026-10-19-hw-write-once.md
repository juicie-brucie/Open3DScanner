- 2026-10-19 — the hw-test agent's record rule is now explicit: **write-once once handed over**. Corrections are the
  dispatcher's before merge (one line, verifier wording) or a dated addendum after landing; a new run is a new record. This
  is the rule the worktree sync's "untracked ... differs" refusal has been enforcing implicitly — three corrected records
  tripped it today. The sync helper now moves such a copy aside (outside the repo) instead of refusing, so a corrected
  record can never block a sync again.
