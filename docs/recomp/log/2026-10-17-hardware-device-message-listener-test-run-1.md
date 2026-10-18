- 2026-10-17 — hardware (`wt-hw`): `device_message_listener_test` run 4 — the #11
  controlled re-run: 4th consecutive reproduction; refined reading (trailing
  device-changed event races the main thread's entry into `obscan_context_destroy`).
