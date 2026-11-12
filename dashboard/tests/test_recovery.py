import json, tempfile, unittest, sqlite3
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock
from app import Monitor, create_app, TOPIC, ACK_TOPIC, HELLO_TOPIC, SESSION_TOPIC

class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.path=Path(self.tmp.name)/'readings.sqlite3'
        self.m=Monitor(self.path,'localhost');self.m.connected=True
        self.client=Mock()
    def tearDown(self):self.m.stop();self.tmp.cleanup()
    def sample(self,**kw):
        d=dict(device_id='board1',session_id='a'*32,seq=1,uptime_ms=1000,
               temperature_c=27.5,alert=True,reporting_mode='adaptive',max_interval_ms=10000,
               sample_age_ms=0,recovered=False,buffer_count=1,dropped_samples=0)
        d.update(kw);return json.dumps(d).encode()
    def send(self,raw):
        self.m._message(self.client,None,SimpleNamespace(topic=TOPIC,retain=False,payload=raw))
    def test_commit_before_ack_and_dedup(self):
        def check(topic,payload,**kwargs):
            self.assertEqual(topic,ACK_TOPIC)
            self.assertEqual(payload,'a'*32+':1')
            with sqlite3.connect(self.path) as db:self.assertEqual(db.execute('SELECT COUNT(*) FROM readings').fetchone()[0],1)
        self.client.publish.side_effect=check
        self.send(self.sample());self.send(self.sample(sample_age_ms=2000,recovered=True))
        self.assertEqual(self.m.snapshot()['total'],1)
        self.assertEqual(self.m.snapshot()['duplicates'],1)
        self.assertEqual(self.client.publish.call_count,2)
    def test_replay_is_historical_not_live(self):
        self.send(self.sample())
        self.send(self.sample(seq=2,uptime_ms=2000,recovered=True,sample_age_ms=60000))
        s=self.m.snapshot();self.assertEqual(s['state'],'recovering');self.assertIsNone(s['age_s'])
        self.assertEqual(s['recovered_total'],1)
        self.assertIn('estimated_sample_at_utc',self.m.export_csv())
        self.send(self.sample(seq=3,uptime_ms=63000))
        self.assertEqual(self.m.snapshot()['state'],'live')
    def test_bad_or_conflicting_duplicate_not_acknowledged(self):
        self.send(self.sample());self.client.reset_mock()
        self.send(self.sample(temperature_c=29))
        self.send(self.sample(seq=2,session_id='wrong'))
        self.send(self.sample(seq=2,sample_age_ms=-1))
        self.client.publish.assert_not_called()
        self.assertEqual(self.m.snapshot()['invalid'],3)
    def test_retry_after_dashboard_restart_is_idempotent(self):
        self.send(self.sample());other=Monitor(self.path,'localhost')
        try:
            other._message(self.client,None,SimpleNamespace(topic=TOPIC,retain=False,payload=self.sample()))
            self.assertEqual(other.snapshot()['total'],1)
            self.assertEqual(other.snapshot()['duplicates'],1)
        finally:other.stop()
    def test_reboot_new_session_preserves_both_samples(self):
        self.send(self.sample());self.send(self.sample(session_id='b'*32))
        self.assertEqual(self.m.snapshot()['total'],2)
    def test_hello_assigns_valid_session(self):
        self.m._message(self.client,None,SimpleNamespace(topic=HELLO_TOPIC,retain=False,payload=b'start'))
        args=self.client.publish.call_args
        self.assertEqual(args.args[0],SESSION_TOPIC)
        self.assertRegex(args.args[1],r'^[a-f0-9]{32}$')
    def test_no_ack_when_storage_fails(self):
        self.m.db.close()
        self.send(self.sample())
        self.client.publish.assert_not_called()
        self.m.db=sqlite3.connect(str(self.path),check_same_thread=False)
