# Alliance E-stop and A-stop roles

Select `RED_ALLIANCE` or `BLUE_ALLIANCE` on the configuration page and save/reboot.
Role IDs are 3 and 4; existing hub and FMS Table IDs remain 0, 1, and 2.
Hardware and the allowed input channel range stay fixed for the running boot.

## Wiring and arena channels

Both roles use the original Freezy Estops ESP32-S3/W5500 wiring:

| Function | GPIO | Red channel | Blue channel |
|---|---:|---:|---:|
| Station 1 E-stop | 1 | 1 | 7 |
| Station 1 A-stop | 2 | 2 | 8 |
| Station 2 E-stop | 3 | 3 | 9 |
| Station 2 A-stop | 15 | 4 | 10 |
| Station 3 E-stop | 18 | 5 | 11 |
| Station 3 A-stop | 16 | 6 | 12 |

Pins use `INPUT`, preserving the external circuitry requirement (no internal
pull-up/down). Wire state is `!digitalRead(pin)`: GPIO HIGH sends `false`, which
the arena interprets as asserted. GPIO LOW sends `true`, meaning released.
The sampler reads all six inputs every RTOS tick (requested interval 1 ms).
This is a requested cadence, not a measured timing guarantee.

The configuration page's **Alliance Stop Inputs** section sets a shared E-stop /
A-stop press debounce from 0 to 1000 ms, defaulting to **20 ms**. Save & Reboot
persists it in `alliance/pressDebounceMs` and applies it on the next boot. Zero
disables debounce. Each input has an independent timer: it must remain asserted
continuously for the configured duration before a press enters its delivery
queue. A sampled release cancels a pending press and passes through immediately;
the existing post-ACK release hold still applies. Shorter pulses are ignored.
Buttons held at boot also wait for debounce, with an initial released state
queued while qualification is pending. The input-status page shows raw readings,
so it can show a press before a debounced event is queued. Debounce adds to delivery
latency; queued press timestamps start when the press qualifies.

The W5500 uses SPI2 with SCK 13, MISO 12, MOSI 11, CS 14, IRQ 10, RST 9.
Alliance roles do not initialize hub counters, relays, DMX, field-stop GPIO 33,
or start-button GPIO 34. They do not send hub telemetry or start-match requests.
Legacy sonar/scoring and BARGE_LIGHTS behavior is outside these roles.

## Delivery

The local Freezy arena source was inspected in `web/alternateIO.go` and
`plc/plc.go`: WebSocket `setInput` and HTTP `/api/freezy/eStopState` both write
through `SetAlternateIOStopState`. The E-stop and A-stop getters invert the
stored input, and `ResetEstops` can reset all stored stops to released.

This implementation replaces the legacy loop's synchronous six-input HTTP
POST with acknowledged single-input writes to `/api/plc/websocket`:

```json
{"type":"setInput","data":[{"channel":1,"state":false}]}
```

Only `plcInputSetSuccess` with boolean `success: true` and integer `count: 1`
acknowledges delivery. There is one request in flight across the entire device,
since the arena ACK carries no channel or request ID. Each role is restricted
to its own six channels. Hub send toggles cannot disable stop delivery.
This requires the Freezy arena extensions; upstream compatibility is not assumed.
An ACK confirms the input write, not that the arena or robot has acted on it.

Each input has a 256-entry FIFO, including its debounced boot snapshot. Qualified transitions remain
until acknowledged. Round-robin selection preserves order within each input
and avoids starvation between inputs; there is no global ordering across pins.
An acknowledged assertion holds that input's release for at least 100 ms.
Other inputs can continue sending during the hold. Stable inputs are eligible
for refresh 100 ms after their last ACK, to restore a held stop after an arena
reset. Actual latency depends on network/ACK timing and queued work.

An error, disconnect, malformed ACK, or 500 ms ACK timeout retains history.
Ambiguous ACKs/timeouts close the old connection before retrying. Backoff grows
from 50 ms to 1 s, in addition to WebSocket reconnection time. Sampling runs in
a separate task while the main loop owns all WebSocket calls. Stop roles do
not perform synchronous HTTP login in the WebSocket callback.

If any FIFO overflows or the sampling task cannot start, a fault latches for
the entire alliance controller. Queued history stops draining and all six
inputs are repeatedly sent as asserted as transport permits. Unretained
transitions are counted explicitly. A write already in flight when the fault
occurs cannot be recalled. Network loss still prevents remote delivery.

Recover from a latched fault only on a stopped bench: keep the arena/robots
disabled, inspect wiring and connectivity, capture diagnostics, correct the
cause, then reboot the controller and verify all six inputs before resuming.
Reboot clears RAM queues and the fault; there is no automatic fault clear.

## Status LEDs and diagnostics

The configuration page's **View Input Status** link opens `/inputs`. It shows
the six local E/A button readings, station labels, GPIOs, and any latched fault.
The page polls the read-only `/api/inputs` endpoint every 500 ms without
overlapping requests. Sampler snapshots are copied under the input lock; JSON
serialization and HTTP delivery happen after releasing that lock. Readings
become unavailable when sampling is at least one second old, a request fails,
or no successful update arrives for two seconds. Displayed input states do not
confirm remote delivery and can differ from forced stop outputs during a fault.

The legacy GPIO 47, GRB, 750-pixel output configuration is retained with
brightness 15 and a 900 mW power limit. Only the first three pixels are used:

- 0: 500 ms blinking heartbeat; dim white for successful delivery, orange for
  failed delivery, red while offline, black when no result was recorded.
- 1: solid alliance color; flashing red on a latched fault (also distinct from
  the normal solid red alliance indicator).
- 2: dim green/black activity based on parsed WebSocket message count.

Other pixels stay off. The LED page describes this fixed configuration and
does not allow changes to the LED layout. Alliance roles can configure the
winner strobe delay and on-time there (whole seconds, 0–3600); these use the
same `winDelaySec` and `winDurationSec` settings as hub winner lights. GPIO 41
activates only for the matching alliance win coil after the configured delay,
then turns off after the configured on-time. An on-time of zero disables the
strobe. Five-second serial summaries report fault/loss counts, per-input queue
depth/high-water marks, ACKs/retries, and maximum sampling, loop, rendering, and
delivery timings.

## Settings migration

On first boot, legacy `settings/deviceRole` values `RED_ALLIANCE`,
`BLUE_ALLIANCE`, and `FMS_TABLE` are recognized when `role/role` is absent.
Network and arena settings use the existing FMS migration: `useDHCP`,
`deviceIP`, `deviceGWIP`, `arenaIP`, and `arenaPort` populate the new namespaces
only where their destination keys are missing. The role marker is written last.
Original keys remain intact; an already configured hub role takes precedence.
Existing hub configuration can select either new role manually.

## Follow-up: arena load and batching

Once the full arena is set up, the user plans to monitor arena scan times with
all controllers connected. Use those measurements, alongside controller ACK
latency, retries, and queue depths, to decide whether traffic optimization is
needed. Batching is a future option; current delivery behavior is unchanged.

Consider combining the six stable input refreshes from each alliance controller
into one `setInput` message with one acknowledgment. This could reduce refresh
request/ACK counts by roughly sixfold at the same refresh interval. Verify arena
batch support and acknowledgment semantics first: the current controller accepts
only `success: true` with `count: 1`. Batch acknowledgment tracking would need to
identify exactly which writes succeeded before consuming any queued history.

Preserve per-input press/release ordering, debounce, release holds, retry
retention, and fault behavior. In particular, do not collapse a queued press
and release into a released snapshot. Evaluate batching stable refreshes first,
while retaining prompt delivery of qualified input changes.

## Validation

`pio run -e esp32-s3-devkitm-1` builds all roles in the same firmware.
`test/alliance_policy_test.cpp` contains compile-only assertions for channel/pin
mapping, six-input refresh, retry retention, independent release holds, overflow,
in-flight faults, and timestamp wraparound. Compile using the installed ESP32
C++ compiler with `-std=c++14 -Wall -Wextra -Werror -fsyntax-only`.

The firmware build and compile-only assertions for both alliance and existing
FMS policies passed. The build reports an existing deprecated PCNT-driver
warning from the hub counter module.

Simulation/socket tests require explicit user approval and were skipped for
this change at the user's request. No hardware was flashed or tested.
Outstanding bench checks: each E/A input on both roles, simultaneous presses,
rapid press/release, held stops across arena reset, cable loss/reconnect,
delayed/malformed ACKs, overflow recovery, NVS migration, LEDs, and regression
checks for both hub roles and FMS_TABLE. Compilation does not establish
end-to-end stop timing or robot behavior.
