import unittest
from tools.analyze_alert_timing import analyze

class TimingAnalysisTests(unittest.TestCase):
    def row(self,seq=31,state='True'):
        return dict(session_id='a'*32,seq=str(seq),uptime_ms='33000',alert=state)
    def line(self,seq=31,rtt=155,state='ON',recovered=0):
        return f"ALERT_ACK session={'a'*32} seq={seq} sample_ms=33000 saved_ack_rtt_ms={rtt} state={state} recovered={recovered}"
    def test_join_and_separate_recovery_roundtrips(self):
        log='\n'.join([self.line(),self.line(),self.line(seq=45,rtt=120,state='OFF'),self.line(seq=60,rtt=40000,recovered=1)])
        s=analyze(log,[self.row(),self.row(45,'False'),self.row(60)])
        self.assertEqual(len(s['events']),3)
        self.assertEqual(s['summary']['connected_ON']['median_ms'],155)
        self.assertEqual(s['summary']['connected_OFF']['median_ms'],120)
        self.assertEqual(s['summary']['recovery_ON']['median_ms'],40000)
    def test_reject_old_data_without_traces(self):
        with self.assertRaises(ValueError):analyze('Sample 31: 28 C', [self.row()])
    def test_reject_unknown_or_inconsistent_event(self):
        for rows in ([],[self.row(state='False')]):
            with self.assertRaises(ValueError):analyze(self.line(),rows)
    def test_reject_conflicting_repeat(self):
        with self.assertRaises(ValueError):analyze(self.line()+'\n'+self.line(rtt=200),[self.row()])

    def test_detected_event_without_ack_is_reported(self):
        log='ALERT_EVENT seq=31 sample_ms=33000 state=ON\nALERT_EVENT seq=45 sample_ms=40000 state=OFF\n'+self.line()
        s=analyze(log,[self.row()])
        self.assertEqual(s['detected_event_trace_count'],2)
        self.assertFalse(s['all_logged_events_acknowledged'])
        self.assertEqual(s['detected_events_without_saved_ack'][0]['seq'],45)
