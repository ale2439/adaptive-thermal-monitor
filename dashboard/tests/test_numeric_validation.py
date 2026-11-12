"""Firmware-width bounds must be checked before binding numbers into SQLite."""
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from app import Monitor, TOPIC, validate_payload

class NumericValidationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.m = Monitor(Path(self.tmp.name)/'db.sqlite3', 'localhost')
        self.client = Mock()

    def tearDown(self):
        self.m.stop(); self.tmp.cleanup()

    def payload(self, **values):
        data = dict(device_id='board1',session_id='a'*32,seq=1,uptime_ms=1000,
                    temperature_c=27.5,alert=False)
        data.update(values)
        return json.dumps(data).encode()

    def deliver(self, raw):
        self.m._message(self.client, None, SimpleNamespace(topic=TOPIC,retain=False,payload=raw))

    def test_uint32_endpoints_validate(self):
        for field in ('seq','uptime_ms'):
            for value in (0,4294967295):
                with self.subTest(field=field,value=value):
                    self.assertEqual(validate_payload(self.payload(**{field:value}))[field], value)

    def test_uint32_endpoints_store_and_ack(self):
        for value in (0,4294967295):
            self.deliver(self.payload(seq=value,uptime_ms=value))
        self.assertEqual(self.m.snapshot()['total'],2)
        self.assertEqual(self.client.publish.call_count,2)
        self.assertFalse(self.m.db.in_transaction)

    def test_bad_numbers_rejected_without_ack_or_storage_change(self):
        self.deliver(self.payload());self.client.reset_mock()
        previous = self.m.snapshot()['latest']
        invalid = (-1,4294967296,9223372036854775808,10**1000,True,False,1.0,'1',None)
        for field in ('seq','uptime_ms'):
            for value in invalid:
                with self.subTest(field=field,value=str(value)[:25]):
                    raw=self.payload(**{field:value})
                    with self.assertRaises(ValueError):validate_payload(raw)
                    self.deliver(raw)  # Must not propagate OverflowError or other exceptions.
        self.client.publish.assert_not_called()
        state=self.m.snapshot()
        self.assertEqual(state['total'],1)
        self.assertEqual(state['latest'],previous)
        self.assertEqual(state['invalid'],len(invalid)*2)
        self.assertFalse(self.m.db.in_transaction)

    def test_huge_temperature_cannot_overflow_math_isfinite(self):
        # Range-check the integer before a conversion to C double in isfinite().
        for value in (10**1000,-10**1000,float('nan'),float('inf')):
            self.deliver(self.payload(temperature_c=value))
        self.client.publish.assert_not_called()
        self.assertEqual(self.m.snapshot()['total'],0)
        self.assertEqual(self.m.snapshot()['invalid'],4)
