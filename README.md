# Adaptive Thermal Monitor with Outage Recovery

A working FRDM-MCXN947 temperature-monitoring demonstration. The board reads its P3T1755 sensor over I3C, evaluates a hysteresis alert, and publishes JSON over Ethernet/MQTT to a local Flask dashboard. It reduces routine traffic while keeping one-second sensing, and buffers samples during communication interruptions.

## Recorded results

| Experiment | Observed result |
|---|---|
| Fixed baseline | 866 consecutive readings over 869.52 seconds |
| Adaptive reporting | 30 readings over 284.48 seconds; 89.75% lower observed message rate |
| Cable interruption | 53 recovered readings, sequences 51–103 with no gaps |
| Recovery integrity | Zero dropped samples and zero duplicate stored IDs in this test |
| Replay | 12.496 seconds from first to last recovered arrival |
| Alert test | ON at 28.000°C, held ON at 27.875°C, OFF at 27.750°C |

These are individual measured runs, not a controlled benchmark or a guarantee for every network. The fixed/adaptive temperatures and time windows differ. Message reduction is not a measured energy saving. Tests of larger outages and repeated trials remain future work.

Raw CSV exports and a reproducible calculation are in `results/` and `tools/analyze_results.py`. Exports contain earlier sessions too; the script selects the relevant sessions/segments. Original timestamps are UTC.

## Reporting and recovery

- Sample approximately every second; report a 0.25°C change from the last queued report, an alert change, or a ten-second heartbeat.
- Final demonstration thresholds: alert ON ≥28.0°C, OFF ≤27.8°C. Older fixed/adaptive/outage measurements used 26.0/25.7°C; the final alert test used 28.0/27.8°C.
- A 300-sample RAM ring keeps unconfirmed history. On missing dashboard ACK or MQTT disconnection, replay proceeds oldest first.
- The dashboard commits to SQLite before sending an application ACK. A session ID plus sequence prevents duplicate storage during retries, including after dashboard restart.
- Recovered records are historical; telemetry returns to Live only after a current record. Estimated sample times subtract age at transmission from receipt time; network transit delay remains unknown.

## Quick start

Follow [setup](docs/SETUP.md) for Mac, Mosquitto, MCUXpresso import and UART settings. Follow [demonstration](docs/DEMO.md) for the live test sequence. [Validation notes](docs/VALIDATION.md) describe what is verified and what is still limited.

| Directory | Contents |
|---|---|
| `firmware/frdmmcxn947_thermal_recovery_cm33_core0` | Complete MCUXpresso project, including the SDK sources used |
| `dashboard` | Flask/Paho app, browser UI and unit tests |
| `config` | Mosquitto LAN demo configuration |
| `results/raw` | Four original recorded CSV exports |
| `results/summary.json` | Recomputed experiment metrics |
| `tests/native` | Buffer and reporting-policy tests, outside the IDE project |
| `tools` | Reproducible results analysis |
| `.github/workflows` | Software checks for pushes and pull requests |

## Run software checks

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r dashboard/requirements.txt
(cd dashboard && python -m unittest discover -s tests -v)
python tools/analyze_results.py --check
cc -std=c99 -Wall -Wextra -Werror tests/native/test_buffer.c -o /tmp/test_buffer
/tmp/test_buffer
cc -std=c99 -Wall -Wextra -Werror tests/native/test_policy.c -o /tmp/test_policy
/tmp/test_policy
```

GitHub Actions checks the dashboard, recorded evidence and native policy tests. It does not compile the complete MCUXpresso firmware or exercise a physical board.

## Limits

The ring is RAM: reset or power loss erases it. Full capacity drops the oldest reading and counts the drop. Approximately five minutes of *total retained history* fits; pre-outage adaptive samples also use slots. Sensing starts after initial DHCP acquisition. Alerts remain locally evaluated during replay, but dashboard notification may wait behind the backlog. No fan/cooling actuator, remote threshold control, flash persistence or synchronized sensor wall-clock time is implemented. Use for short demonstrations rather than 32-bit counter wrap scenarios.

MQTT is anonymous, unencrypted and restricted to a trusted LAN for this demonstration. Do not expose its listener through router port forwarding. Authentication/TLS is future work, not a tested security claim.

## Source notices

This project incorporates NXP MCUXpresso SDK, lwIP, FreeRTOS and other bundled source. Existing copyright and license notices are retained in the corresponding files. This repository does not grant a replacement license for third-party source. Flask and Paho are installed dependencies rather than vendored packages.

This is project setup and validation documentation, not an IEEE course report. The team should write its own report in accordance with course rules.
