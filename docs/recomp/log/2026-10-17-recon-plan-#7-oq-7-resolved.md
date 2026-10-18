- 2026-10-17 — recon (`openscanner/recon`): PLAN #7 OQ-7 resolved → `SPEC-READY`
  (branch `recon/plan7-detection-path`): detection = SDK-dylib libusb hotplug
  (VIDs `0x2BC5`/`0x1B20`) + GVCP net watcher, both started at
  `obscan_context_create`; IOKit matching belongs to the embedded-libusb darwin
  backend at `UsbContext` construction. Header + findings reworded.
