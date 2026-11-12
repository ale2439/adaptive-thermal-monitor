"""Real SQLite lock regression: an uncommitted INSERT must never get an ACK."""
import json
import sqlite3
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from app import Monitor, TOPIC, ACK_TOPIC

class StorageTransactionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'test.sqlite3'
        self.m = Monitor(self.path, 'localhost')
        self.m.db.execute('PRAGMA busy_timeout=1')
        self.client = Mock()
        self.payload = json.dumps(dict(device_id='board1',session_id='a'*32,
            seq=1,uptime_ms=1000,temperature_c=28.0,alert=True)).encode()
        self.message = SimpleNamespace(topic=TOPIC,retain=False,payload=self.payload)

    def tearDown(self):
        self.m.stop()
        self.tmp.cleanup()

    def send(self):
        self.m._message(self.client, None, self.message)

    def persisted_count(self):
        with sqlite3.connect(self.path) as db:
            return db.execute('SELECT COUNT(*) FROM readings').fetchone()[0]

    def test_failed_commit_retry_cannot_ack_pending_row(self):
        reader = sqlite3.connect(self.path)
        try:
            # A real reader lock permits INSERT but prevents writer COMMIT.
            reader.execute('BEGIN')
            reader.execute('SELECT * FROM readings').fetchall()
            for _ in range(2):
                self.send()
            # This fails on the original code: its second delivery sends an ACK.
            self.client.publish.assert_not_called()
            self.assertFalse(self.m.db.in_transaction)
            self.assertEqual(self.persisted_count(), 0)
            self.assertEqual(self.m.snapshot()['total'], 0)
            self.assertIsNone(self.m.last_received_monotonic)
        finally:
            reader.rollback()
            reader.close()
        self.send()
        self.assertEqual(self.persisted_count(), 1)
        self.assertEqual(self.client.publish.call_count, 1)
        self.client.publish.assert_called_with(ACK_TOPIC, 'a'*32+':1', qos=1, retain=False)
        # A genuine committed duplicate is safely acknowledged again.
        self.send()
        self.assertEqual(self.persisted_count(), 1)
        self.assertEqual(self.client.publish.call_count, 2)

    def test_failed_commit_does_not_erase_prior_committed_rows(self):
        self.send()
        self.client.reset_mock()
        data = json.loads(self.payload); data['seq'] = 2;data['uptime_ms'] = 2000
        self.message.payload = json.dumps(data).encode()
        reader = sqlite3.connect(self.path)
        try:
            reader.execute('BEGIN');reader.execute('SELECT * FROM readings').fetchall()
            self.send(); self.send()
            self.client.publish.assert_not_called()
            self.assertEqual(self.persisted_count(),1)
        finally:
            reader.rollback();reader.close()
        self.send()
        self.assertEqual(self.persisted_count(),2)
        self.assertEqual(self.client.publish.call_count,1)

    def test_legacy_pending_row_is_not_mistaken_for_saved_duplicate(self):
        self.m.db.execute('INSERT INTO readings (received_ms,device_id,seq,uptime_ms,temperature_c,alert,session_id) VALUES (?,?,?,?,?,?,?)',
            (0,'board1',1,1000,28.0,1,'a'*32))
        self.assertTrue(self.m.db.in_transaction)
        self.assertEqual(self.persisted_count(),0)
        def verify_saved(*args, **kwargs):
            self.assertFalse(self.m.db.in_transaction)
            self.assertEqual(self.persisted_count(),1)
        self.client.publish.side_effect = verify_saved
        self.send()
        self.assertEqual(self.client.publish.call_count,1)
        self.assertEqual(self.m.duplicates,0)

if __name__ == '__main__':unittest.main()
