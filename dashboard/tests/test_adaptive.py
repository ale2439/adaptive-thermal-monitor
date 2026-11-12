import csv,io,json,sqlite3,time
from test_dashboard import DashboardTests
from app import Monitor

class AdaptiveTests(DashboardTests):
    def test_heartbeat_freshness_and_export(self):
        self.monitor.connected=True
        self.monitor.accept(self.sample(max_interval_ms=10000, reporting_mode='adaptive'))
        self.monitor.last_received_monotonic=time.monotonic()-12
        s=self.monitor.snapshot()
        self.assertEqual(s['state'],'live')
        self.assertEqual(s['stale_after_s'],23)
        self.assertEqual(s['reporting_mode'],'adaptive')
        self.monitor.last_received_monotonic=time.monotonic()-24
        self.assertEqual(self.monitor.snapshot()['state'],'stale')
        rows=list(csv.DictReader(io.StringIO(self.monitor.export_csv())))
        self.assertEqual(rows[0]['max_interval_ms'],'10000')
        self.assertEqual(rows[0]['reporting_mode'],'adaptive')

    def test_bad_policy_rejected(self):
        for interval in (True,0,999,60001, '10000'):
            with self.assertRaises(ValueError):
                self.monitor.accept(self.sample(max_interval_ms=interval))

    def test_legacy_database_migration(self):
        path=self.temp.name+'/legacy.sqlite3'
        db=sqlite3.connect(path)
        db.execute('CREATE TABLE readings (id INTEGER PRIMARY KEY, received_ms INTEGER NOT NULL, device_id TEXT NOT NULL, seq INTEGER NOT NULL, uptime_ms INTEGER NOT NULL, temperature_c REAL NOT NULL, alert INTEGER NOT NULL)')
        db.execute("INSERT INTO readings VALUES(1,1,'board1',1,1000,27,1)")
        db.commit();db.close()
        m=Monitor(path,'localhost')
        try:
            self.assertEqual(m.snapshot()['latest']['reporting_mode'],'fixed')
            self.assertEqual(m.snapshot()['latest']['max_interval_ms'],1000)
        finally:m.stop()
