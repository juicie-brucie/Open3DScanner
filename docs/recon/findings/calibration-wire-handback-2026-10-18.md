HAND-BACK TO RECON — calibration store, confirmed and corrected from the wire
Date: 2026-10-18
From: an out-of-band protocol capture of a full CrealityScan session (launch to
quit) on the localhost link to Creality's RPCServer daemon. Wire observation
only; no Creality binary read. Pairs against: docs/recon/findings/calibration-
store.md (row #94 / OQ-S15, by recon-1).

Provenance for tagging: these are RUNTIME-OBSERVED facts (a live device read),
so claims below can move from [inferred] toward [hw-confirmed] where noted —
BUT the capture was taken outside tests/results/ and is not a gated run record.
Treat this as a lead for recon to fold in and re-confirm through the normal
path, not as a results artifact.

=============================================================================
1. CONFIRMED (wire agrees with the static findings)
=============================================================================

  - FLASH-READ TRANSPORT EXISTS AND IS OBSERVABLE WITHOUT libOrbbecSDK.
    The host reads device flash with vendor opcode 25 (memory read), tunnelled
    through the RPC method ob_rpc_send_data, framed with the standard vendor
    header (magic 0x47 'MG', size/opcode/requestId). Responses use the
    0x4252 'BR' response framing, 768 bytes per read. 120 such reads in the
    captured session.
    -> Bears on OQ-S15d (the flash transport): the TRANSPORT half is now
       observed on the wire. (The backup-blob SCHEMA half is still open — it
       dispatches into libOrbbecSDK and was not on the wire; see section 3.)

  - RECORD HEADER FORMAT HOLDS. Every record read starts with the 16-byte
    header the findings describe: {magic, checksum, length, type}. Nine records
    were seen at startup/project-open.

  - LASER-PLANE RECORD IS WHERE THE FINDINGS SAY. A magic-0x...14 / length-0x143c
    record sits at flash 0x211000, with a SECOND copy at 0x213000 (the findings'
    default backup read key, from table 0x2cebeb0). The two copies' checksums
    differ — consistent with the findings' backup-vs-user split.

  - calibration-store.md's core map (the 0x46447314 / type 0x211000 laser record,
    the backup key 0x213000) matches observed reality.

=============================================================================
2. CORRECTIONS / REFINEMENTS (wire differs from, or sharpens, the findings)
=============================================================================

  C1. THE MAGIC LOW BYTE VARIES BY RECORD KIND. The findings give the plane magic
      as the single constant 0x46447314. On the wire, the high 3 bytes are
      constant (0x464473) and the LOW byte is a per-kind tag. Observed kinds:
        0x08, 0x11, 0x12, 0x13, 0x14 (planes), 0x15.
      So 0x46447314 is specifically the laser-plane KIND, not the universal
      record magic. Suggest the findings describe magic as 0x464473<kind>.

  C2. THE "type" FIELD EQUALS THE RECORD'S OWN FLASH ADDRESS. For every record,
      header.type == its flash offset (e.g. the 0x211000 record has type
      0x211000; the 0x0b0000 record has type 0xb0000). The findings call out
      type 0x211000 for planes as if a constant; it is really "this record's
      address". Minor, but it changes how to interpret type generally.

  C3. WRITE KEY 0x212000 HAS NO RECORD AT IT. The findings say planes are written
      via updateFlashData(0x46447314, 0x212000, ...). On the wire the plane
      records are at 0x211000 and 0x213000; nothing starts at 0x212000. Either
      0x212000 is a staging/scratch region (not where the live record lands) or
      the write-key read is off by a page. Worth re-checking the write-side
      disassembly against these observed landing addresses.

  C4. PLANE RECORD LENGTH IS 5180 B (0x143c payload), not 5120. The findings
      list the LIVE USER COPY as 5120 B (0x1400, from get_line_laser_light_
      plane_param). The FLASH record payload is 5180 B. The 60-byte difference
      is unaccounted for — header/trailer vs struct body, or extra fields in
      flash not copied to the live struct. (Sizes to reconcile: flash read
      8192 B window; flash record payload 5180 B; live copy 5120 B;
      LineLaserParams 5488 B; SoC files 4096 B — the findings already flag these
      as "sizes to keep straight", and 5180 is a new point in that set.)

=============================================================================
3. NOT SEEN IN THIS SESSION (still needs recon's own path)
=============================================================================

  - THE CHECKSUM ALGORITHM IS UNCONFIRMED. The header's second word is a
    checksum over the payload. It is NOT a standard CRC-32 (any common variant)
    and NOT a plain 32-bit XOR of words — neither reproduces the observed values
    across the nine records. The findings point at a device lookup table
    (0x2ceb980) for the XOR-checksum; that table is the authoritative source.
    Recon should derive the algorithm from that table rather than from the wire
    — I deliberately did not brute-force it.

  - THE 0x14000 TEMPERATURE-CALIBRATION RECORD WAS NOT READ in this session
    (getCalibrationTempInfoFromLineLaserParam, key 0x14000). It is likely read
    only during a temperature-compensation or calibration flow, not at normal
    scan startup. Relevant to OQ-S15e.

  - THE PLANE PAYLOAD INTERNALS (OQ-S15a) are still opaque from the wire — the
    capture gives the record bytes but not the field semantics/units. Decoding
    needs the struct layout from recon's side (the three 8-float tables, which
    is "the plane").

  - THE BACKUP/RESUME BLOB SCHEMA (obscan_device_management_backup_device_param,
    vtable+0x200) did not appear on the wire — it dispatches inside libOrbbecSDK.
    Settle from libOrbbecSDK_arm64.dylib in the recon cache or a runtime hexdump,
    as the findings already say.

=============================================================================
4. BONUS: THE FULL FLASH MAP READ AT STARTUP / PROJECT-OPEN
=============================================================================

  Address     magic(low)  length        when read
  ---------   ----------  -----------    ----------------
  0x200000    0x11        6780 (0x1a7c)  startup (t~2.9s)   primary
  0x202000    0x11        6780           startup            copy of 0x200000
  0x204000    0x08        2952 (0xb88)   startup
  0x20a000    0x12        6646 (0x19f6)  startup
  0x20e000    0x13       12066 (0x2f22)  startup
  0x211000    0x14        5180 (0x143c)  startup            LASER PLANES
  0x213000    0x14        5180           startup            plane backup copy
  0x215000    0x15       12066 (0x2f22)  startup            (same size as 0x20e000)
  0x0b0000    0x08        3916 (0xf4c)   project-open (t~25s, not startup)

  Reads are contiguous 768-B windows over 0x200000..0x206000 and
  0x20a000..0x218000 at startup, plus 0x0b0000..0x0b2000 when the project opens.
  Pairing 0x20e000 (kind 0x13) with 0x215000 (kind 0x15) — same length, adjacent
  — may be another primary/backup pair of a different record family; worth a
  look.

=============================================================================
5. SUGGESTED RECON ACTIONS
=============================================================================

  1. Fold C1-C4 into calibration-store.md as corrections (magic-is-per-kind,
     type==address, the 0x212000 write-key discrepancy, the 5180-B flash length).
  2. Derive the checksum from table 0x2ceb980 (static), then validate it against
     the nine observed records — I can provide the record bytes if useful.
  3. Keep OQ-S15a/b/e open; this hand-back does not close them. OQ-S15d's
     transport half can be noted as wire-observed; its schema half stays open.
  4. Treat all of the above as [inferred, wire-observed 2026-10-18] until a
     gated tests/results run re-confirms through the normal path.

Record bytes, the capture, and the decode script are outside the repo at
~/DevReposiMac/raptor-protocol-capture/ and the session scratchpad; available
on request. Nothing was written to the repo or to any worktree.
