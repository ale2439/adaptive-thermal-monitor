# SQLite acknowledgment and RGB LED fixes

## SQLite

Previously, an INSERT followed by a failed COMMIT could leave a row pending on the same SQLite connection. A retry found that row and returned through the duplicate branch without a successful commit. The MQTT handler then sent an acknowledgment, permitting the board to discard an unsaved reading.

`Monitor.accept` now holds its lock around a transactional SQLite context. The context rolls back when an INSERT or COMMIT raises. Any leftover pending transaction is rolled back before duplicate detection. Duplicate counting, freshness updates and ACK eligibility occur only after the transactional context exits successfully. A committed duplicate is still acknowledged again to support retry after lost ACK.

Regression tests use an actual second SQLite connection holding a read lock so INSERT succeeds but COMMIT fails. They verify two attempts send no ACK and leave no pending row, release the lock, verify retry becomes committed and acknowledged, and check an older committed row survives the failure. A separate test covers a leftover pending row. These tests fail on the old implementation and pass with the fix.

Run from repository root:

```bash
source .venv/bin/activate
(cd dashboard && python -m unittest discover -s tests -v)
```

Tests intentionally exercising locks may print `database is locked` errors. A final `OK` means the expected failure paths behaved correctly.

## RGB LED

The added header `thermal_led.h` uses the FRDM-MCXN947 SDK board mapping:

| State | Colour | Pad |
|---|---|---|
| Valid temperature, alert clear | Green | P0_27 |
| Valid temperature, alert active | Red | P0_10 |
| Waiting for a valid sample or sensor read/init failure | Blue | P1_2 |

The board LED outputs are active low. Initialization enables PORT0/PORT1 and GPIO0/GPIO1 clocks, preloads all outputs off, configures the pads for GPIO, then shows blue. The app updates colour after each sensor result, using the same hysteresis state published over MQTT. Network loss does not override a valid local alert with a connection colour. Colour updates do not wait for dashboard acknowledgment or replay.

Startup initialization runs before DHCP. Sensing itself still begins after initial DHCP, as before. The LED pins do not overlap this project's Ethernet or I3C signal pads.

Run the native GPIO-stub test from repository root:

```bash
cc -std=c99 -Wall -Wextra -Werror -Itests/native/led_mocks tests/native/test_led.c -o /tmp/test_led
/tmp/test_led
```

This checks observable output states and startup configuration with GPIO stubs; it does not prove physical LED operation. Build/flash and verify actual colours on the board. Updated GitHub Actions runs this test as well as the existing checks.

## Physical acceptance test

1. Start Mosquitto and the fixed dashboard. Build and flash the updated recovery firmware. Resume from main.
2. Blue should show until the first valid reading.
3. With a valid reading <=27.8°C, expect green. Warm the sensor vicinity until >=28.0°C, expect red. Remove the heat source and let it return to <=27.8°C, expect green. Between thresholds, preserve the previous state.
4. Repeat an Ethernet interruption while leaving USB power connected. The LED should keep tracking the local temperature. Reconnect and verify stored recovery readings.
5. Export a fresh CSV and capture evidence of physical colours. Existing result CSVs predate this fix and are retained as historical evidence, not evidence of the new LED behavior.

A prior sensor alert remains in history if the sensor fails, while the physical LED becomes blue to indicate the reading is not valid. Blue is not an MQTT connection indicator.
