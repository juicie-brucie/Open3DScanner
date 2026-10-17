# Open3DScanner — Claude Code notes

Hardware runs go through the `hw-test` subagent. Do not run
`device_message_listener_test` or any other SDK tool directly from the main
session.

Hardware tests run from the worktree that holds the interposer build:

```
/Users/brucemackay/DevReposiMac/Open3DScanner-wt-hw
```

Never from the main checkout — it lacks the interposer and the 180 s timeout.

Results go to `tests/results/` with the raw log alongside.

A `PreToolUse` hook (`.claude/hooks/guard-hw-run.sh`) enforces the sharp edges:
it blocks setting `DYLD_INSERT_LIBRARIES`, touching `.recon-cache/` or vendor
dylibs, editing `obscan_api.h`, and running the hardware test from the wrong
tree.
