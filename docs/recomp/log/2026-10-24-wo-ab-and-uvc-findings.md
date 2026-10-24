# 2026-10-24 — WO-A/WO-B land; the OQ-UVC1 static+test; the calibration erratum

- **#516/WO-A** — per-frame pose (Horn's quaternion) + multi-frame fusion (incremental/batch/ICP);
  the device-free tests; the cloud_compare NN refactor.
- **#521/WO-B** — FastSurface/QualitySurface reconstruction, mesh A/B (symmetric Hausdorff, RMS
  surface distance, watertightness), the recon pipeline, the CLI, the OMSH live-mesh wire format,
  and the device-free live-surface demo. The vendor-mesh A/B remains gated on #207/#211/#206.
- **#522/OQ-UVC1** — the vendor claim is the classic non-exclusive `USBInterfaceOpen` (no
  seize/entitlement); the empirical gap is stated and the decisive runtime test named (the
  non-seize classic as user/root with UVCAssistant owning). #171/#178 stay blocked until it runs.
- **#525/#204 follow-up** — the calibration-store §4 erratum (the file layout is open; the
  named-negative transforms; the next probe recon1's REA) and the `uvc-claim-macos.md` (root,
  no entitlement; the helper reconciliation).
- **The repo config** — #523 untracked the root `CLAUDE.md`; #524 ignores the local Claude/agent
  configs and the Xcode build dir. The #516/#521 branches were rebuilt clean (no config in the
  history) and the feature heads no longer carry it.
- **Then:** the xcode-main non-seize implementation → the hw runtime test (the #171/#178 gate);
  xcode-recon resumes #212 + the A/B methodology; #207 needs a scene-matched vendor recording;
  #206 the emitter-line association; #208 the intrinsics values.
