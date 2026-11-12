# Validation and provenance

The four CSVs are original user-exported measurements, renamed for clarity without changing their bytes. `tools/analyze_results.py` recomputes `results/summary.json` and can assert the recorded milestones with `--check`.

## Segments used

- **Fixed:** last uninterrupted sequence run in `fixed_baseline.csv`, sequence 4413–5278, 866 rows. Earlier interruption/burst data is excluded.
- **Adaptive:** rows explicitly marked adaptive in `adaptive_reporting.csv`, 30 rows over 284.48 seconds. 134 earlier fixed rows are excluded.
- **Outage:** newest nonempty session in `outage_recovery.csv`. Recovered sequences 51–103 are consecutive; all 60 rows in that session report zero drops and have unique session/sequence IDs. Replay rows include samples acquired during draining. The 12.496-second value is first-to-last recovered *receipt span*, not total outage time or necessarily exact return-to-live latency.
- **Alert:** newest nonempty session in `alert_transitions.csv`, eight rows. Initial Normal at27.750°C, ON at28.000°C, still ON at27.875°C, OFF at27.750°C. Firmware clear threshold was27.800°C; sensor quantization means the observed crossing sample was27.750°C.

Message-rate reduction compares `(N-1)/elapsed` for each run, avoiding treating the first sample as an elapsed interval. This is a descriptive comparison across different temperature traces, not a matched replay experiment. Payload sizes changed between stages, so publication-count reduction must not be reported as an equivalent wire-byte reduction.

## Code provenance

The final source is reconstructed from the supplied SDK projects and the exact MQTT-buffer and threshold edits used in the recorded tests. The startup log is also updated to print the new thresholds; that text-only correction does not change the tested policy. The packaged final tree has software checks, but has not been separately flashed as a byte-identical build. Preserve the working local project and do not claim reproducible binary identity with the measurement firmware.

## Checks

- Arm GCC compilation of firmware sources, including the effective 1024-byte MQTT output buffer check.
- Native policy tests for thresholds, reporting delta/heartbeat, timer wrap; native ring tests for overflow, dropped count, ordered replay and repeated ACK.
- Dashboard tests for malformed telemetry, persistence, legacy schema migration, adaptive freshness, deduplication, boot sessions, storage-before-ACK and no ACK on failed storage.
- Local actual-broker MQTT integration checked session assignment, delivery/ACK, duplicate retry, dashboard restart, and recovered-to-live transition before the final threshold-only packaging edits.

## Scope not established by these tests

No statistical reliability/latency guarantee, repeated outage study, power-loss persistence, secure broker authentication/TLS, large-network scalability, calibrated ambient temperature accuracy, or energy savings. The local sensor measures temperature near the device. Alerts use demonstration thresholds rather than validated equipment limits.
