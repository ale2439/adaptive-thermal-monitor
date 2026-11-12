"""Local MQTT temperature dashboard. Run: python app.py --broker 10.0.0.249."""
import argparse
import csv
import io
import json
import logging
import math
import re
import uuid
import sqlite3
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

from flask import Flask, Response, jsonify, render_template
import paho.mqtt.client as mqtt

LOG = logging.getLogger("thermal")
TOPIC = "thermal/board1/readings"
HELLO_TOPIC = "thermal/board1/hello"
SESSION_TOPIC = "thermal/board1/session"
ACK_TOPIC = "thermal/board1/ack"


def validate_payload(raw):
    """Reject malformed telemetry; never invent readings or infer a sensor alert."""
    if len(raw) > 4096:
        raise ValueError("Message exceeds 4096 bytes")
    data = json.loads(raw)
    if not isinstance(data, dict) or data.get("device_id") != "board1":
        raise ValueError("Expected device_id board1")
    temperature = data.get("temperature_c")
    if isinstance(temperature, bool) or not isinstance(temperature, (int, float)):
        raise ValueError("temperature_c must be numeric")
    if not math.isfinite(temperature) or not -100 <= temperature <= 200:
        raise ValueError("temperature_c is outside accepted display range")
    if type(data.get("alert")) is not bool:
        raise ValueError("alert must be a JSON boolean")
    for field in ("seq", "uptime_ms"):
        if type(data.get(field)) is not int or data[field] < 0:
            raise ValueError(f"{field} must be a nonnegative integer")
    interval = data.get("max_interval_ms", 1000)
    if type(interval) is not int or not 1000 <= interval <= 60000:
        raise ValueError("max_interval_ms must be an integer from 1000 to 60000")
    mode = data.get("reporting_mode", "fixed")
    if mode not in ("fixed", "adaptive"):
        raise ValueError("Unknown reporting_mode")
    result = {key: data[key] for key in ("device_id", "seq", "uptime_ms", "temperature_c", "alert")}
    session = data.get("session_id")
    if session is not None and (not isinstance(session, str) or not re.fullmatch(r"[0-9a-f]{32}", session)):
        raise ValueError("Invalid session_id")
    age = data.get("sample_age_ms", 0)
    if type(age) is not int or not 0 <= age <= 4294967295:
        raise ValueError("Invalid sample_age_ms")
    recovered = data.get("recovered", False)
    if type(recovered) is not bool:
        raise ValueError("Invalid recovered flag")
    for field in ("buffer_count", "dropped_samples"):
        if type(data.get(field, 0)) is not int or not 0 <= data.get(field, 0) <= 4294967295:
            raise ValueError("Invalid buffer counter")
    return dict(result, max_interval_ms=interval, reporting_mode=mode,
                session_id=session, sample_age_ms=age, recovered=recovered,
                buffer_count=data.get("buffer_count", 0), dropped_samples=data.get("dropped_samples", 0))


class Monitor:
    def __init__(self, db_path, broker, mqtt_port=1883):
        self.broker, self.mqtt_port = broker, mqtt_port
        self.lock = threading.RLock()
        self.db = sqlite3.connect(str(db_path), check_same_thread=False)
        self.db.execute("""CREATE TABLE IF NOT EXISTS readings (
            id INTEGER PRIMARY KEY, received_ms INTEGER NOT NULL,
            device_id TEXT NOT NULL, seq INTEGER NOT NULL, uptime_ms INTEGER NOT NULL,
            temperature_c REAL NOT NULL, alert INTEGER NOT NULL)""")
        columns = {row[1] for row in self.db.execute("PRAGMA table_info(readings)")}
        if "max_interval_ms" not in columns:
            self.db.execute("ALTER TABLE readings ADD COLUMN max_interval_ms INTEGER NOT NULL DEFAULT 1000")
        if "reporting_mode" not in columns:
            self.db.execute("ALTER TABLE readings ADD COLUMN reporting_mode TEXT NOT NULL DEFAULT 'fixed'")
        additions = {
            "session_id": "TEXT", "sample_age_ms": "INTEGER NOT NULL DEFAULT 0",
            "recovered": "INTEGER NOT NULL DEFAULT 0", "buffer_count": "INTEGER NOT NULL DEFAULT 0",
            "dropped_samples": "INTEGER NOT NULL DEFAULT 0"}
        for name, declaration in additions.items():
            if name not in columns:
                self.db.execute(f"ALTER TABLE readings ADD COLUMN {name} {declaration}")
        self.db.execute("CREATE UNIQUE INDEX IF NOT EXISTS message_identity ON readings(session_id,seq) WHERE session_id IS NOT NULL")
        self.db.commit()
        self.duplicates = 0
        self.last_arrival = None
        self.connected = False
        self.detail = "Connecting to broker…"
        self.invalid = 0
        self.last_received_monotonic = None
        self.client = None

    def accept(self, raw):
        data = validate_payload(raw)
        now = int(time.time() * 1000)
        fields = ("device_id", "seq", "uptime_ms", "temperature_c", "alert", "max_interval_ms",
                  "reporting_mode", "session_id", "sample_age_ms", "recovered", "buffer_count", "dropped_samples")
        with self.lock:
            # A failed COMMIT may leave an INSERT visible only on this connection.
            # Never let that pending row enter the duplicate/ACK path.
            if self.db.in_transaction:
                self.db.rollback()
            duplicate = False
            with self.db:
                # sqlite3's context manager commits on success and rolls back if
                # the body OR the commit itself raises (e.g. SQLITE_BUSY).
                if data["session_id"] is not None:
                    previous = self.db.execute(
                        "SELECT uptime_ms,temperature_c,alert FROM readings WHERE session_id=? AND seq=?",
                        (data["session_id"], data["seq"])).fetchone()
                    if previous is not None:
                        if previous != (data["uptime_ms"], data["temperature_c"], int(data["alert"])):
                            raise ValueError("Conflicting duplicate sample")
                        duplicate = True
                if not duplicate:
                    self.db.execute("INSERT INTO readings (received_ms," + ",".join(fields) + ") VALUES (" +
                                    ",".join("?" for _ in range(len(fields)+1)) + ")",
                                    (now,) + tuple(data[k] for k in fields))
            # No return, freshness update or ACK eligibility before commit succeeds.
            if duplicate:
                self.duplicates += 1
                return data
            self.last_arrival = time.monotonic()
            if data["recovered"]:
                self.last_received_monotonic = None
            else:
                self.last_received_monotonic = time.monotonic() - data["sample_age_ms"] / 1000
        return data

    def start(self):
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        self.client.reconnect_delay_set(min_delay=1, max_delay=10)
        self.client.on_connect = self._connect
        self.client.on_disconnect = self._disconnect
        self.client.on_connect_fail = self._connect_fail
        self.client.on_message = self._message
        self.client.connect_async(self.broker, self.mqtt_port, keepalive=15)
        self.client.loop_start()

    def _connect(self, client, userdata, flags, reason_code, properties):
        with self.lock:
            self.connected = reason_code == 0
            self.detail = "Connected; waiting for telemetry" if self.connected else f"Connection refused: {reason_code}"
        if reason_code == 0:
            result, _ = client.subscribe([(TOPIC, 1), (HELLO_TOPIC, 1)])
            if result != mqtt.MQTT_ERR_SUCCESS:
                with self.lock:
                    self.detail = "Connected, but subscription could not be sent"
            LOG.info("Connected to %s:%s; subscribing to %s", self.broker, self.mqtt_port, TOPIC)

    def _disconnect(self, client, userdata, flags, reason_code, properties):
        with self.lock:
            self.connected = False
            self.detail = "Broker disconnected; retrying…"
        LOG.info("MQTT disconnected: %s", reason_code)

    def _connect_fail(self, client, userdata):
        with self.lock:
            self.connected = False
            self.detail = "Broker unavailable; retrying…"

    def _message(self, client, userdata, message):
        if message.topic == HELLO_TOPIC and not message.retain:
            # Board accepts the first assignment after reboot and keeps it in RAM.
            client.publish(SESSION_TOPIC, uuid.uuid4().hex, qos=1, retain=False)
            return
        if message.topic != TOPIC:
            return
        # Retained messages are historical; don't mark them as fresh live sensor data.
        if message.retain:
            LOG.info("Ignoring retained snapshot; waiting for live telemetry")
            return
        try:
            data = self.accept(message.payload)
            if data["session_id"] is not None:
                client.publish(ACK_TOPIC, f"{data['session_id']}:{data['seq']}", qos=1, retain=False)
        except (ValueError, TypeError, UnicodeError) as error:
            with self.lock:
                self.invalid += 1
            LOG.warning("Rejected telemetry: %s", error)
        except sqlite3.Error:
            LOG.exception("Could not store telemetry")
            with self.lock:
                self.detail = "Storage error; check terminal"

    def snapshot(self):
        fields = ("id", "received_ms", "seq", "uptime_ms", "temperature_c", "alert", "max_interval_ms",
                  "reporting_mode", "session_id", "sample_age_ms", "recovered", "buffer_count", "dropped_samples")
        with self.lock:
            rows = self.db.execute("SELECT " + ",".join(fields) + " FROM readings ORDER BY id DESC LIMIT 300").fetchall()[::-1]
            total, minimum, maximum, recovered_total = self.db.execute(
                "SELECT COUNT(*),MIN(temperature_c),MAX(temperature_c),COALESCE(SUM(recovered),0) FROM readings").fetchone()
            age = None if self.last_received_monotonic is None else time.monotonic() - self.last_received_monotonic
            connected, detail, invalid = self.connected, self.detail, self.invalid
            arrival_age = None if self.last_arrival is None else time.monotonic() - self.last_arrival
            duplicates = self.duplicates
        history = [dict(zip(fields, row)) for row in rows]
        for row in history:
            row["alert"] = bool(row["alert"])
            row["recovered"] = bool(row["recovered"])
            row["sampled_ms"] = row["received_ms"] - row["sample_age_ms"]
        arrival = history[-1] if history else None
        # Within a boot session, order by sensor uptime, not burst arrival time.
        latest = max((r for r in history if r["session_id"] == arrival["session_id"]),
                     key=lambda r: r["uptime_ms"]) if arrival and arrival["session_id"] else arrival
        interval = latest["max_interval_ms"] if latest else 1000
        stale_after_s = max(5, interval / 1000 * 2 + 3)
        replay = bool(arrival and arrival["recovered"] and connected and arrival_age is not None and arrival_age < 3)
        live = connected and age is not None and age < stale_after_s and not replay
        state = "recovering" if replay else "live" if live else ("stale" if history else "waiting")
        # Sort each displayed history by estimated sample time, not receipt time.
        history.sort(key=lambda r: r["sampled_ms"])
        return {"broker": self.broker, "mqtt_port": self.mqtt_port, "topic": TOPIC,
                "broker_connected": connected, "detail": detail, "state": state,
                "age_s": None if age is None else round(age, 1), "history": history,
                "latest": latest, "total": total, "minimum": minimum, "maximum": maximum, "invalid": invalid,
                "recovered_total": recovered_total, "duplicates": duplicates,
                "buffer_count": arrival["buffer_count"] if arrival else 0,
                "dropped_samples": arrival["dropped_samples"] if arrival else 0,
                "alert_on_c": 28.0, "alert_off_c": 27.8, "stale_after_s": stale_after_s,
                "reporting_mode": latest["reporting_mode"] if latest else "waiting", "max_interval_ms": interval}

    def export_csv(self):
        fields = ("received_ms", "device_id", "seq", "uptime_ms", "temperature_c", "alert", "max_interval_ms",
                  "reporting_mode", "session_id", "sample_age_ms", "recovered", "buffer_count", "dropped_samples")
        with self.lock:
            rows = self.db.execute("SELECT " + ",".join(fields) + " FROM readings ORDER BY id").fetchall()
        output = io.StringIO()
        writer = csv.writer(output)
        writer.writerow(["received_at_utc"] + list(fields[1:]) + ["estimated_sample_at_utc"])
        for row in rows:
            data = dict(zip(fields, row))
            data["alert"] = bool(data["alert"])
            data["recovered"] = bool(data["recovered"])
            writer.writerow([datetime.fromtimestamp(row[0] / 1000, timezone.utc).isoformat()] +
                [data[k] for k in fields[1:]] +
                [datetime.fromtimestamp((row[0] - data["sample_age_ms"]) / 1000, timezone.utc).isoformat()])
        return output.getvalue()

    def stop(self):
        if self.client:
            self.client.disconnect()
            self.client.loop_stop()
        with self.lock:
            self.db.close()


def create_app(monitor):
    app = Flask(__name__)

    @app.get("/")
    def index():
        return render_template("index.html")

    @app.get("/api/status")
    def status():
        response = jsonify(monitor.snapshot())
        response.headers["Cache-Control"] = "no-store"
        return response

    @app.get("/export.csv")
    def export():
        return Response(monitor.export_csv(), mimetype="text/csv",
                        headers={"Content-Disposition": "attachment; filename=thermal_readings.csv"})

    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker", default="10.0.0.249")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--db", default=str(Path(__file__).with_name("thermal_recovery.sqlite3")))
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    monitor = Monitor(args.db, args.broker, args.mqtt_port)
    monitor.start()
    LOG.info("Open http://127.0.0.1:%s in your browser", args.port)
    try:
        create_app(monitor).run(host="127.0.0.1", port=args.port, debug=False, use_reloader=False)
    finally:
        monitor.stop()


if __name__ == "__main__":
    main()
