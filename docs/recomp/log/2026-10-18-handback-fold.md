- 2026-10-18 — recon folds the operator's **wire hand-back** into the calibration findings (`d0f0133`): the
  flash-read transport confirmed (opcode 25, 'MG'/'BR' framing, 768-B reads, 120 in the captured session),
  four corrections (magic `0x464473<kind>`; header `type` == the record's own flash address; the 0x212000
  write-key discrepancy; the 5180-B flash payload), and the full startup/project-open flash map — all tagged
  `[inferred, wire-observed 2026-10-18]` (not a gated run), with the hand-back preserved verbatim alongside.
