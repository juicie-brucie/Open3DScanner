# Addendum — 2026-10-21 — caveat on step 4 trigger in 2026-10-21-plan170-helper-claim.md

Issued: 2026-10-21. Original record is write-once and is not edited.

## Caveat

The original record describes step 4 as "USB cable unplugged by user while helper
was serving." The coordinator has since clarified: **the user power-cycled the
scanner before physically pulling the USB cable.** The IOKit service-termination
interest that fired `claimed -> releasing (unplug)` may therefore have been
triggered by the device powering off (USB device removal on power-down) rather
than by a cable disconnection while the device was powered.

## What is and is not confirmed by this run

**Confirmed:** the IOKit termination-interest handler wired in
`iokit_device_claim.mm` fires and drives the claim state machine through
`claimed -> releasing (unplug) -> released (unplug: release complete)` when the
USB device is removed — regardless of whether removal is caused by power-down or
cable pull.

**Not confirmed by this run:** that the same handler fires on a cold cable
disconnection while the device remains powered. A run with the scanner powered on
and only the USB cable physically removed would isolate that path.

## Board impact

The step 4 PASS in `2026-10-21-plan170-helper-claim.md` stands for the
IOKit-removal event path. Whether "power-off" and "cable-pull" share the same
removal event or differ is not determinable from this run. The #170 row move
(`HW` → `DONE`) proposed in that record remains; this caveat is an open question
for the next row that covers unplug-and-replug behaviour.
