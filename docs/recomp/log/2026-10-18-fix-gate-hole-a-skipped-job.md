- 2026-10-18 — fix: gate hole — a skipped job's check run satisfied the required
  `verdict` context, so agent-tier PRs could auto-merge without a verdict
  (proven by #67's fix head `a88d576`). The `verdict` job is renamed
  `verdict-comment`; the context is the status only. #67's fix head is being
  read post-merge for the record.
