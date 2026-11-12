"""Summarize NEW board-clock sample-to-saved-ACK round trips from a serial log.
This measures neither physical crossing-to-detection nor browser paint latency.
"""
import argparse
import csv
import json
import re
import statistics
from pathlib import Path

EVENT = re.compile(r'ALERT_EVENT seq=(\d+) sample_ms=(\d+) state=(ON|OFF)')
ACK = re.compile(r'ALERT_ACK session=([a-f0-9]{32}) seq=(\d+) sample_ms=(\d+) saved_ack_rtt_ms=(\d+) state=(ON|OFF) recovered=([01])')

def analyze(serial_text, rows):
    indexed={(r.get('session_id'),r['seq']):r for r in rows if r.get('session_id')}
    events={}
    for match in ACK.finditer(serial_text):
        session,seq,sample_ms,rtt,state,recovered=match.groups()
        key=(session,seq)
        if key not in indexed:
            raise ValueError(f'ACK has no matching exported CSV row: {key}')
        row=indexed[key]
        if int(row['uptime_ms'])!=int(sample_ms) or (row['alert']=='True')!=(state=='ON'):
            raise ValueError(f'CSV does not match ACK event: {key}')
        if any(int(v)>4294967295 for v in (seq,sample_ms,rtt)):
            raise ValueError('Timing trace exceeds uint32 range')
        event=dict(session_id=session,seq=int(seq),state=state,sample_ms=int(sample_ms),
                   sample_to_saved_ack_ms=int(rtt),recovered=recovered=='1')
        if key in events and events[key]!=event:
            raise ValueError(f'Conflicting repeated ACK log: {key}')
        events[key]=event
    if not events:
        raise ValueError('No ALERT_ACK lines. Capture a NEW instrumented run; old CSVs cannot supply this measurement.')
    sessions={e['session_id'] for e in events.values()}
    if len(sessions)!=1:
        raise ValueError('Analyze one boot session per serial log; split logs containing multiple boots.')
    detected={(int(seq),int(ms),state) for seq,ms,state in EVENT.findall(serial_text)}
    acknowledged={(e['seq'],e['sample_ms'],e['state']) for e in events.values()}
    unacknowledged=sorted(detected-acknowledged)
    groups={}
    for label,replay in [('connected',False),('recovery',True)]:
        for state in ('ON','OFF'):
            values=[e['sample_to_saved_ack_ms'] for e in events.values() if e['recovered']==replay and e['state']==state]
            groups[label+'_'+state]=dict(count=len(values),minimum_ms=min(values) if values else None,
                median_ms=statistics.median(values) if values else None,maximum_ms=max(values) if values else None)
    return dict(metric='board sample timestamp to application storage ACK observed on board',
                includes='queueing, network, SQLite commit, return ACK and up to one app polling period',
                excludes='physical threshold crossing before sampling and browser display latency',
                events=list(events.values()),summary=groups,
                detected_event_trace_count=len(detected),
                detected_events_without_saved_ack=[dict(seq=seq,sample_ms=ms,state=state) for seq,ms,state in unacknowledged],
                all_logged_events_acknowledged=bool(detected) and not unacknowledged,
                coverage_note='No detected event trace; missed ACK coverage is unknown' if not detected else 'Check unacknowledged events separately; do not count as zero delay')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial',required=True,type=Path)
    p.add_argument('--csv',required=True,type=Path)
    p.add_argument('--out',type=Path)
    a=p.parse_args()
    with a.csv.open(newline='') as f:rows=list(csv.DictReader(f))
    try:result=analyze(a.serial.read_text(),rows)
    except ValueError as e:p.error(str(e))
    text=json.dumps(result,indent=2)+'\n'
    if a.out:a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
