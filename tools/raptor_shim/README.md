# raptor_shim — the Phase D RPC shim (row #172)

A localhost msgpack-RPC server that stands in for the Creality daemon's
`ob_rpc_*` surface, serving frames from an offline source. Its job is to let
`raptor_client` (and later the app) run unchanged against our own process
instead of the vendor daemon. Live USB is a later row: the source is an
interface, and the live capture plugs into it.

Spec: `docs/recon/findings/protocol-spec.md` (§1 methods, §2 vendor framing,
§5 discovery), `docs/recon/findings/frame-codec.md` §10.3 (the 11-element
array), `docs/recon/findings/frame-band-rule.md` (#174: the whole frame, not a
band), and `docs/recon/findings/property-semantics-row101.md` (the property
map). Wall: `tools/` + `engine/`; no header edit, no product switch.

## What it serves

| method | result |
|---|---|
| `ob_rpc_start_stream<suffix>` | `true`; opens the interface's source |
| `ob_rpc_stop_stream<suffix>` | `true`; closes it |
| `ob_rpc_get_frame<suffix>` | the 11-element array, or the no-data form |
| `ob_rpc_send_data<suffix>` | the vendor response: GET/SET_PROPERTY served from #101's map, other opcodes refused with `errorCode=1` |
| `ob_get_device_enumeration_info` | four entries: device id, VID/PID, serial, name, interface index, host, port (10010/10011/10012/10013) |
| `ob_get_stream_profile_list<suffix>` | `[[0, 9, 1920, 1200, 50]]` |
| `ob_rpc_get_pu` / `get_pu_range` / `set_pu` | `6000` / `[1000, 15000, 100, 1000]` / `true` |
| `ob_set_ir_frame_light_type<suffix>` | `true` |
| anything else | rpclib's error form with the daemon's wording: `could not find function '<name>' with argument count <n>` |

The suffixes and ports are protocol-spec §1.1's: IR `0-2-12` on 10013 (stream
type 6), depth `0-2-10` on 10010 (stream type 7). A requested stream type that
does not match the interface is accepted and logged, not silently corrected.

### The frame array

`[0, e1, e2, e3, bin(e4), len(e4), bin(e6), len(e6), bin(e8), len(e8), e10]`

`e1`/`e2` come from the source frame, `e3` is its format, `e4` is the whole
frame (1920×1200×1) — `frame-band-rule.md` §4: no band assembly. The no-data
form is the same array with all three buffers empty and `e10 = -1`.

### Discovery

The GVCP listener recognises the spec's 8-byte `DISCOVERY_CMD` on UDP 3956 and
logs it. It does **not** answer: protocol-spec §5/§6.4 does not record the
daemon's `DISCOVERY_ACK`, and inventing those bytes would be worse than an
unanswered command. The port map is available over
`ob_get_device_enumeration_info`, which is the path the app already uses.

The listener is **off by default** (`--discovery-bind`): binding 3956 would
take discovery traffic from a running vendor daemon.

## Running it

The shim listens on **its own ports** (20013 IR, 20010 depth; the
`daemon_switch.h` defaults), so the vendor daemon can keep 10013/10010.

```sh
# the verified #132 bundles (outside the repo, never committed)
build/tools/raptor_shim/raptor_shim \
    --ir-bundle ~/captures/plan132-ir-v2 \
    --depth-bundle ~/captures/plan132-depth-v2

# then, with the switch on, from another terminal:
build/tools/raptor_client/raptor_client --live 127.0.0.1 20013 5 /tmp/ir.raw \
    --daemon shim --dump-replies
build/tools/raptor_client/raptor_client --live 127.0.0.1 20010 5 /tmp/depth.raw \
    --daemon shim --dump-replies
```

`--ir-port 10013 --depth-port 10010` puts the shim on the vendor's ports
instead (only sensible while the vendor daemon is stopped).

Each interface cycles its bundle when it reaches the end (`wraps=` in the
closing line), so a multi-second run keeps pulling frames from a finite
capture. There is no frame-rate throttle: every `get_frame` gets the next
frame, which is a 100% data share; the vendor daemon's ~50 fps / empty-poll
behaviour is #144's measurement, not something the shim imitates.

Flags: `--host`, `--ir-port`, `--depth-port`, `--ir-bundle`, `--depth-bundle`,
`--source none|bundle|live`, `--helper-service NAME`, `--helper-status`,
`--no-ir`, `--no-depth`, `--seconds N`, `--discovery-bind IP:PORT`, `--verbose`.

## The daemon switch (row #179)

Which daemon the host talks to is an explicit, logged choice
(`engine/include/openscanner/daemon_switch.h`):

| selection | ports |
|---|---|
| `vendor` (default) | IR 10013, depth 10010 — the product's current behaviour |
| `shim` | IR 20013, depth 20010 — this process |

```sh
raptor_client --live 127.0.0.1 20013 5 /tmp/ir.raw --daemon shim
# [live] daemon=shim (OpenScanner RPC shim, IR 20013 / depth 20010;
#        the vendor daemon is neither stopped nor replaced)
```

With the flag absent the client dials the vendor ports and prints nothing new,
so a default run is unchanged to the byte. The switch never starts or stops a
daemon — that is the operator's or the installer's act. The app does not read
the switch yet; `raptor_client` is its first consumer.

## The live source (row #179)

`--source live` selects the live frame source instead of a bundle. The adapter
(`live_source.h`) sits on `raw_capture`'s transport seam and refuses, naming
every missing piece, until three things exist:

1. **the helper's claim** — the shim never claims the USB device (row #170 owns
   it). `HelperClaimChannel` asks the helper's XPC `status`; without a claiming
   helper the source is refused. The helper must be registered with launchd for
   the session: the **user LaunchAgent** (`org.openscanner.helper`, row #181,
   installed by `tools/macos_helper/install_user_agent.sh install` — no sudo).
   Check it without starting the shim:

   ```sh
   build/tools/raptor_shim/raptor_shim --helper-status
   # [shim] helper status on org.openscanner.helper: claimed-by-helper
   ```

   Exit 0 means the service answered (claimed or not); 1 means it did not, and
   the detail names the fix (an unregistered service says so explicitly).
2. **the transport's descriptor map** — `raw_capture`'s UVC source names the
   first unknown field (`docs/recon/findings/usb-descriptor-map.md`).
3. **the frame's OB words** — `HostClockObWords` (row #180) supplies them from
   their settled sources (`docs/recon/findings/rpc-endpoint-map.md` §2.2):

   | word | source |
   |---|---|
   | `e1` | `CLOCK_REALTIME` µs, stamped at the frame's dequeue |
   | `e2` | `CLOCK_MONOTONIC` µs, the same dequeue |
   | `e3` | **255**, the observed OBFormat (380/380 replies); the descriptor table has no such field |
   | `e10` | the device's IR light type — **no source yet**, so a named refusal |

   The UVC PTS is the *device* clock and is **not** written into `e1`/`e2`: the
   RPC's two time words are host clock domains (the daemon fills them from the
   PAL frame's timestamps), and labelling a device clock as `CLOCK_REALTIME`
   would be a guess. `e10` varies per frame (3/4 on both interfaces), so a
   per-stream constant would be wrong; it refuses until the transport reports
   the device's light state or a finding binds the control path to it.

Today it refuses:

```
$ raptor_shim --source live --no-depth --seconds 1
raptor_shim: live source refused: not-configured: the device is not claimed by
openscanner-helper (openscanner-helper is not answering on
org.openscanner.helper (...)); not-configured: IR endpoint (...);
not-configured: e10 (the IR frame's IR light type) has no source: the UVC
transport does not report the device's light state, and no finding binds the
laser/exposure state to it
```

When #178's transport lands and an `IrLightState` source exists, the same
`FrameSource` seam serves live frames — no change above it.

Bounds: a request larger than 1 MiB closes the connection; every send has a
5 s socket bound and a failed write closes the connection. One poll loop
serves every connection, so the frame source is touched from one thread only.

## Tests

`raptor_shim_test` is device-free and in `ctest -LE hardware`:

- the array layout (elements, lengths, the no-data form), the discovery
  command match, the vendor framing (GET/SET and an unknown opcode), and the
  interface table;
- a synthetic capture bundle written with the recorder's own writer, served
  through the shim, then the **real** `raptor_client --live 127.0.0.1 10013`
  against it: the client's own tally must show data, its saved frame must be
  the bundle's first frame, and its dump must show the 11-element array;
- a bare method name must get the daemon's refusal, and the enumeration must
  carry the four-entry port map;
- a source with nothing to give must produce the client's no-data tally, not
  errors;
- the switch: its default, its parse and log line, the shim's ports; the live
  seam's three refusals and its mapping with all three supplied; the switch-on
  path end to end (the client on the shim's port with `--daemon shim`) and the
  same port refused with the switch off;
- the live CLI's clean `not-configured` refusal.

The test uses the IR port 10013 and skips cleanly if something already owns
it (the vendor daemon, when it is running).

## The hardware card: A/B against the vendor daemon

Run once per interface, with the scanner attached. The point is that the same
capture, the same client and the same property loop work against both servers.

1. **Vendor side** (the daemon running, the product as shipped):

   ```sh
   build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 10 /tmp/vendor-ir.raw \
       --dump-replies --capture ~/captures/ab-vendor-ir
   build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 10 /tmp/vendor-depth.raw \
       --dump-replies --capture ~/captures/ab-vendor-depth
   ```

2. **Shim side**: stop the vendor daemon (operator decision — the shim never
   does it), then serve the *same scene* from the bundle captured in step 1:

   ```sh
   build/tools/raptor_shim/raptor_shim --ir-bundle ~/captures/ab-vendor-ir \
       --depth-bundle ~/captures/ab-vendor-depth --seconds 30
   # another terminal:
   build/tools/raptor_client/raptor_client --live 127.0.0.1 10013 10 /tmp/shim-ir.raw --dump-replies
   build/tools/raptor_client/raptor_client --live 127.0.0.1 10010 10 /tmp/shim-depth.raw --dump-replies
   ```

3. **Compare** the two client logs and the two saved frames: the tally's
   `other=0` and `error=0` on both, the dump line's `result=array(11)` and
   `[4]=Bin(2304000)` on both, and the saved frame's bytes against the
   bundle's first frame. The A/B is on *the same scene*, so the arrays' `e4`
   may differ per frame but their shape and `e3` must not.

What the card must record: the two commands, each tally line, and whether the
shim's frames were byte-identical to the bundle the vendor run produced.
