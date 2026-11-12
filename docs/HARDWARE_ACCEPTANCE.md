# Fresh hardware acceptance and outstanding experiments

**Status: pending on the physical board.** Software tests, mocked GPIO and old CSV exports cannot complete this acceptance. Earlier green/red/green operation was user-reported; startup blue and local alert transitions during Ethernet disconnection still require a fresh recorded trial. Do not describe this checklist or the synthetic policy model as a completed experiment.

## Build and flash

1. Apply this patch in the Git repository. Stop the active MCU debug session.
2. If MCUXpresso uses an imported workspace copy, copy these updated files from the repository firmware project's `source` into that workspace `source`: `mqtt_freertos.c`, `thermal_buffer.h`, `thermal_logic.h`. Retain the already installed `thermal_led.h` and `lwip_mqtt_freertos.c` from the preceding fix. No project or file deletion is needed.
3. Clean and Build that recovery project; record the build result. Start the updated dashboard and broker. Debug the recovery project once; Resume if paused at main. Record the board revision, source commit and IDE version.
4. Film the board and dashboard together. Keep USB power connected during every Ethernet interruption. Avoid touching exposed circuitry; use a hand near the sensor to vary its temperature. Do not use liquids or intense heat.

## Required fresh observations

| Check | Acceptance | Record actual observation |
|---|---|---|
| Startup | Blue before a valid sample | PENDING |
| Valid normal | Green with alert clear | PENDING |
| Heating | Red when sampled temperature reaches >=28.0°C | PENDING |
| Cooling | Green when sampled temperature reaches <=27.8°C | PENDING |
| Ethernet unplugged | Serial sampling continues; dashboard eventually Stale | PENDING |
| Heat while unplugged | Local green-to-red transition still occurs | PENDING |
| Cool while unplugged | Local red-to-green transition still occurs | PENDING |
| Reconnect | Recovery drains, then Live; inspect recovered events and drop counter | PENDING |

Startup blue may be very brief when DHCP is fast. If it is not visible in a recording, cold-start with Ethernet disconnected to observe blue while waiting for initial DHCP; reconnect to continue. This is an observation of waiting state, not proof of offline-start sensing (which remains unsupported). Do not inject a sensor failure merely to obtain blue.

## Measure the storage acknowledgment round trip

The firmware now logs:

```
ALERT_EVENT seq=31 sample_ms=33000 state=ON
ALERT_ACK session=<32 hex characters> seq=31 sample_ms=33000 saved_ack_rtt_ms=<measured value> state=ON recovered=0
```

The second line is emitted only after a matching dashboard application ACK is observed. Its elapsed time uses the board's clock, from the alert sample timestamp to ACK handling. It includes queueing, outbound network, SQLite commit, return network and up to one app polling period. It is **not** physical threshold crossing-to-detection latency and does **not** measure browser paint delay. Startup, replay and disconnected trials must not be mixed with normal connected timing.

1. Capture one boot session per serial log as plain text, including startup/session and `ALERT_EVENT`/`ALERT_ACK` lines.
2. While connected, perform at least five ON and five OFF transitions. Record all attempts, including ones with no ACK; do not select only quick successes.
3. Export a NEW dashboard CSV from the same run. Keep both original files.
4. Run from repository root:

```bash
python tools/analyze_alert_timing.py --serial /path/to/trial_serial.txt --csv /path/to/trial.csv --out results/new_trial/alert_ack_timing.json
```

The tool joins ACK events to actual stored CSV rows using session/sequence and sensor uptime. It reports connected/recovery ON/OFF counts, minimum, median and maximum. The summary lists detected events lacking a saved ACK; review these separately; absence of an ACK measurement is not a zero delay. Old exports without the new serial trace cannot establish this metric.

## Evaluate missed excursions without overclaiming

`tests/native/test_excursions.c` exercises the actual C hysteresis/reporting policy with ideal pulses of 100, 200, 500, 1000 and 2000 ms at ten onset phases relative to a one-second sampler. It compares adaptive publication with the original fixed one-second reporting. It also checks that a sampled ON/OFF excursion is retained behind a pending acknowledgment rather than coalesced away.

Run:

```bash
cc -std=c99 -Wall -Wextra -Werror tests/native/test_excursions.c -o /tmp/test_excursions
/tmp/test_excursions
```

This is a **synthetic software model**. It identifies subsecond excursions missed by the sampler and checks that adaptive publication adds no misses in the ideal immediate-ACK model. It does not reproduce sensor conversion/filtering, heat transfer, GPIO hardware, actual network timing, losses or all interleavings.

For a physical missed-excursion measurement, use a suitable independent faster temperature reference and record its time series alongside the board output. Predefine a qualifying excursion by crossing the ON threshold and then the OFF threshold. Count reference excursions, board-detected ON events and delivered ON events separately, including phase/duration and any buffer drops. Compare fixed/adaptive modes under matched inputs. The board's own one-second samples cannot establish whether a shorter physical excursion occurred between those samples. Without that reference, label the physical evaluation PENDING and report only the software model and observed alert transitions.

The buffer/sequence remains bounded. Power loss, overflow and unobserved sensor excursions can still lose history. Existing CSVs are preserved as prior evidence; no new physical results are fabricated by this update.

## Current ideal-input model results

`results/MODEL_excursions.csv` is generated by the native model, not by a board. Across ten onset phases, the 100/200/500 ms pulses were detected in 1/2/5 cases respectively; 1000 and 2000 ms pulses were detected in all ten cases. Adaptive and fixed one-second reporting delivered the same detected-event counts under the model assumptions. Detected pulses had at most 900 ms onset-to-sample delay in the tested phases. This is not a physical latency measurement, and the exact sampled detection rates do not include the sensor's conversion time or filtering.
