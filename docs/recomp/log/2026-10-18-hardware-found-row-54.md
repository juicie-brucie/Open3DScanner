- 2026-10-18 — hardware: attempting the #41/#48 smoke found the page itself dead —
  `ui/app.js` fails to parse (a duplicate `const frame`, introduced by V4/#48); the
  client-side confirmation is blocked, the server-side run continued. Filed as #54;
  a hot-fix is in flight. The gate has no JS syntax check — that gap is next.
