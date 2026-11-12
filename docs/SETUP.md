# Setup and restart

Recorded environment: Apple Silicon Mac, MCUXpresso IDE 25.6.136, SDK 26.9.00, Python 3.14.3, Homebrew Mosquitto 2.1.2. Software tests also run with Python 3.12.

## Connections

USB MCU-LINK to Mac provides power/debug and a USB serial port. Board Ethernet connects to a router LAN port; Mac Wi-Fi must be on the same LAN. The board has no built-in Wi-Fi in this setup.

Recorded addresses: Mac/broker `10.0.0.249`, board `10.0.0.103`, gateway `10.0.0.1`. DHCP can change these. Check the Mac with `networksetup -getinfo Wi-Fi`. Update the listener IP in `config/mosquitto-demo.conf`, `EXAMPLE_MQTT_SERVER_HOST` in the firmware `source/mqtt_freertos.c`, and dashboard `--broker` together if necessary. The board's own IP is printed over UART.

## Broker

```bash
brew install mosquitto
/opt/homebrew/opt/mosquitto/sbin/mosquitto -c config/mosquitto-demo.conf -v
```

Keep this terminal open. If port 1883 is already in use, stop the earlier broker process you started; do not launch a second broker on that port. Optional subscriber in another terminal:

```bash
mosquitto_sub -h 10.0.0.249 -t 'thermal/board1/#' -v
```

## Dashboard

From repository root:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r dashboard/requirements.txt
python dashboard/app.py --broker 10.0.0.249
```

Open `http://127.0.0.1:8765`. Start this before the recovery firmware; it assigns a boot-session ID and acknowledges saved samples. Run only one dashboard process. Stop a prior dashboard with Control-C before starting this copy. A separate `thermal_recovery.sqlite3` is created beside this app; databases are excluded from Git.
## Firmware

1. Terminate every active MCU debug session. A suspended session is still active.
2. File → Import → General → Existing Projects into Workspace. Select root directory `firmware/frdmmcxn947_thermal_recovery_cm33_core0`, enable Copy projects into workspace if importing a separate working copy, Finish.
3. Select that project → Clean → Build. Launch Debug for this project once. If paused at `main`, Resume.
4. Open the serial terminal on the board USB modem at 115200 baud, 8 data bits, no parity, 1 stop bit. The recorded port was `/dev/cu.usbmodemTIEPEGZZWCOWF3`; the exact name may change. Do not choose `/dev/cu.debug-console`.
5. Expect DHCP/IP output, `Dashboard session assigned`, and one-second samples with `buffer`/`dropped` counters. Once connected, steady-state MQTT reports are about ten seconds apart.

The MQTT output ring must be 1024 bytes; the recovery JSON exceeds 256 bytes. The final source overrides the generated setting and includes a compile-time minimum-size check. `MQTT queue failed: -1` means an lwIP memory/queue error and needs investigation, rather than repeated Debug launches.

## Thresholds

`source/thermal_logic.h` uses integer millidegrees: 28000 ON and 27800 OFF. The dashboard API, chart lines and labels use 28.0/27.8. Keep them consistent if changing policy. Broker configuration and thresholds are demonstration settings, not equipment safety limits.

