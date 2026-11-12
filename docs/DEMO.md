# Demonstration guide

Prepare the board, router, Mac, broker, one dashboard and serial terminal. Use the final recovery firmware with 28.0/27.8°C thresholds. Keep exported evidence and a backup screen recording available.

## Suggested eight-minute sequence

| Time | Demonstration |
|---|---|
| 0:00–1:00 | Explain purpose: reduce routine MQTT traffic while keeping alerts and recover readings after interruption. Identify sensor, board, Ethernet, broker and dashboard. |
| 1:00–2:00 | Show live temperature, board-reported alert, ten-second MQTT reports and one-second serial samples. Explain the 0.25°C event rule. |
| 2:00–3:15 | Warm the vicinity of the sensor with a hand; show ≥28°C activating the alert. Remove the hand and let it cool to ≤27.8°C. If room temperature prevents a transition, use the recorded CSV evidence and disclose that it is a prior test. |
| 3:15–4:45 | Note last sequence, unplug only Ethernet for 30 seconds while USB remains powered. Show serial sampling/buffer growth and stale dashboard. Reconnect; show recovered rows/backlog drain, then Live. Do not reset the board during this test. |
| 4:45–6:00 | Explain application ACK after SQLite commit and session/sequence deduplication. Distinguish MQTT broker ACK from dashboard storage ACK. |
| 6:00–7:15 | Show measured results: ~89.75% lower observed reporting rate; 53 recovered samples, no sequence gaps/drops/duplicate stored IDs in the recorded outage test; both alert transitions. |
| 7:15–8:00 | State limits and future work: RAM capacity/power loss, no initial-offline sensing, no cooling actuator, no security hardening or energy measurement. |

## Evidence to capture locally

- Dashboard Live and ten-second recent readings.
- Normal, active alert, and cleared alert with visible thresholds.
- Disconnected state with serial sampling and buffer growth.
- Recovery logs/counters and return to Live.
- CSV export and repository software-check results.

Record a short backup video before presenting. Avoid relying on Wi-Fi/router availability during class; explain the network setup used in the recording.

## Likely questions

- **What is adaptive?** Sensing remains about one second; publications depend on temperature change, alert transition or a ten-second heartbeat.
- **Why two thresholds?** Hysteresis avoids repeated ON/OFF switching near one threshold.
- **Why not rely only on MQTT QoS 1?** Broker receipt does not prove the dashboard saved the sample. This app waits for a post-commit application ACK.
- **How are retries recognized?** A random per-boot session ID plus sequence number; uniqueness persists in the dashboard database.
- **How much outage can it tolerate?** 300 total retained samples, roughly five minutes, with oldest-drop counting. Actual available outage history depends on existing retained entries and successful sensor reads.
- **Does it survive power loss?** No; the board buffer is RAM. The Mac SQLite history persists.
- **Does it cool the board?** No actuator is connected; this project monitors and alerts.
- **Is the reduction exactly 90%?** About 90% is the stable-case target. The recorded separate runs show 89.75%; changing temperature and retries can increase traffic.
- **Can all sequence jumps be called loss?** No. Adaptive mode intentionally suppresses samples; evaluate continuity specifically within a recovered interval.
