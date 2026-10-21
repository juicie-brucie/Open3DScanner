# PR landing queue

Owner: dispatch. One landing in flight; the GitHub merge queue is not used.

## Queue — 2026-10-20, round b (post-#414/#416/#417)

| order | PR | head | row | gate | verdict | state | next action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | this records PR | pending | #151/#161/#167/#168 | pending | — | LANDING | land (agent tier: records) |
| 2 | #167 build | pending | #167 | pending | — | IN FLIGHT | build1: device-enumeration retry |
| 3 | #168 build | pending | #168 | pending | — | IN FLIGHT | build2: capture-path hang |
| 4 | P12 | pending | #165 | pending | — | IN FLIGHT | build3: record factory |
| 5 | P9 | pending | #162 | pending | — | IN FLIGHT | recon2: static flash-write falsifier |

**No auto-merge is armed.** #414, #417 and #416 all landed at their verified heads;
the next landing is this records PR, then the four in-flight rows as their PRs open.

## Landed this round (2026-10-20b)

| PR | merge | what |
| --- | --- | --- |
| #414 | `b9e7dc7` | #161 depth-hang finding (finding-only; verified @ `69d8978`) |
| #417 | `ae06afd` | the handoff/board/queue/test-queue pass (script-tier @ `71cb13b`) |
| #416 | `aef16f2` | #161 live RPC bounded timeout (verified @ `723a4b2`) |

## Held / not landing yet

- **#167** — the device-enumeration race: five occurrences, first at 06:23; the race is
  intermittent (two clean starts at 17:00:47 and 17:29:57 in the same window). The fix
  is in build1. **Do not schedule T2 until it lands**, even though the failure is not
  every start.
- **#168** — the `--capture` hang: two IR runs, both exit 143 with zero output;
  build2 owns the fix. T3 waits on it and #161.
- The unlanded 2026-10-20 race/hang/support batch is being landed by the records PR
  (raw logs and crash reports included; no-raw-log records are `_` support files).
  The two `.rec` recordings stay in the hardware room — the landed #71 attempt-4
  record already says so.
- **Binary sha256** is absent in every record of this batch; P12 (#165) makes it a
  generated requirement for future records. It cannot be retro-filled without the
  hardware tester.

## Merge order

1. This records PR.
2. #167 → #168 (the two room blockers).
3. P12 (#165) and P9 (#162).
4. The next hardware card: #161's depth retry, then T2 after #167.

A PR is added here when it opens; it leaves when merged or closed.
