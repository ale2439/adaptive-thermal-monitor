"""Run from the dashboard folder: python -m unittest discover -s tests -v."""
import json
import tempfile
import time
import unittest
from pathlib import Path
from app import Monitor, create_app


class DashboardTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.monitor = Monitor(Path(self.temp.name) / 'readings.sqlite3', '127.0.0.1')
        self.web = create_app(self.monitor).test_client()

    def tearDown(self):
        self.monitor.stop()
        self.temp.cleanup()

    def sample(self, **changes):
        data = dict(device_id='board1', seq=2, uptime_ms=4043, temperature_c=27.75, alert=True)
        data.update(changes)
        return json.dumps(data).encode()

    def test_empty_is_waiting_and_not_normal(self):
        result = self.web.get('/api/status').json
        self.assertEqual(result['state'], 'waiting')
        self.assertIsNone(result['latest'])
        self.assertEqual(result['total'], 0)
        self.assertEqual(self.web.get('/').status_code, 200)

    def test_real_sample_alert_freshness_and_csv(self):
        self.monitor.connected = True
        self.monitor.accept(self.sample())
        result = self.web.get('/api/status').json
        self.assertEqual(result['state'], 'live')
        self.assertTrue(result['latest']['alert'])
        self.assertEqual(result['latest']['temperature_c'], 27.75)
        self.monitor.last_received_monotonic = time.monotonic() - 6
        self.assertEqual(self.web.get('/api/status').json['state'], 'stale')
        self.assertIn('27.75,True', self.web.get('/export.csv').text)

    def test_invalid_data_cannot_replace_latest(self):
        self.monitor.accept(self.sample())
        for raw in (b'not json', self.sample(temperature_c=float('nan')),
                    self.sample(alert='false'), self.sample(seq=-1), self.sample(device_id='other')):
            with self.assertRaises((ValueError, TypeError)):
                self.monitor.accept(raw)
        self.assertEqual(self.monitor.snapshot()['total'], 1)

    def test_history_survives_restart_but_is_stale(self):
        self.monitor.accept(self.sample())
        other = Monitor(Path(self.temp.name) / 'readings.sqlite3', '127.0.0.1')
        try:
            self.assertEqual(other.snapshot()['total'], 1)
            self.assertEqual(other.snapshot()['state'], 'stale')
            self.assertIsNone(other.snapshot()['age_s'])
        finally:
            other.stop()

    def test_disconnect_never_looks_live(self):
        self.monitor.accept(self.sample(alert=False))
        self.monitor.connected = False
        self.assertEqual(self.monitor.snapshot()['state'], 'stale')
        self.assertFalse(self.monitor.snapshot()['latest']['alert'])


if __name__ == '__main__':
    unittest.main()
