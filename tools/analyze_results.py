"""Recompute measured results from original exported CSVs; no synthetic readings."""
import argparse
import csv
import json
from collections import Counter
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def load(name):
    with (ROOT / 'results/raw' / name).open(newline='') as f:
        return list(csv.DictReader(f))

def stamp(row):
    return datetime.fromisoformat(row['received_at_utc'])

def newest_session(rows):
    session = next(row['session_id'] for row in reversed(rows) if row.get('session_id'))
    return [r for r in rows if r.get('session_id') == session]

def summarize():
    fixed = load('fixed_baseline.csv')
    start = len(fixed)-1
    while start > 0:
        current, previous = fixed[start], fixed[start-1]
        if int(current['seq'])-int(previous['seq']) != 1 or not 0 < (stamp(current)-stamp(previous)).total_seconds() < 5:
            break
        start -= 1
    fixed = fixed[start:]
    fd = (stamp(fixed[-1])-stamp(fixed[0])).total_seconds()
    adaptive = [r for r in load('adaptive_reporting.csv') if r['reporting_mode']=='adaptive']
    ad = (stamp(adaptive[-1])-stamp(adaptive[0])).total_seconds()
    outage = newest_session(load('outage_recovery.csv'))
    recovered = [r for r in outage if r['recovered']=='True']
    seq = [int(r['seq']) for r in recovered]
    missing = sorted(set(range(min(seq),max(seq)+1))-set(seq))
    alerts = newest_session(load('alert_transitions.csv'))
    changes = [dict(seq=int(b['seq']), temperature_c=float(b['temperature_c']), alert=b['alert']=='True')
               for a,b in zip(alerts,alerts[1:]) if a['alert']!=b['alert']]
    return {
        'fixed': {'readings':len(fixed),'duration_s':fd,'interval_s':fd/(len(fixed)-1)},
        'adaptive': {'readings':len(adaptive),'duration_s':ad,'interval_s':ad/(len(adaptive)-1),
            'message_rate_reduction_pct':100*(1-((len(adaptive)-1)/ad)/((len(fixed)-1)/fd))},
        'outage': {'session_id':outage[0]['session_id'], 'recovered_readings':len(recovered),
            'first_recovered_seq':min(seq),'last_recovered_seq':max(seq),'missing_recovered_seq':missing,
            'duplicate_stored_ids':len(outage)-len({(r['session_id'],r['seq']) for r in outage}),
            'dropped_samples':max(int(r['dropped_samples']) for r in outage),
            'peak_reported_buffer':max(int(r['buffer_count']) for r in outage),
            'replay_receipt_span_s':(stamp(recovered[-1])-stamp(recovered[0])).total_seconds()},
        'alerts':{'session_id':alerts[0]['session_id'], 'readings':len(alerts),
            'initial_alert':alerts[0]['alert']=='True','transitions':changes,
            'hysteresis_hold_observed':any(r['alert']=='True' and 27.8<float(r['temperature_c'])<28 for r in alerts)}
    }

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--write',action='store_true')
    args=parser.parse_args()
    s=summarize()
    if args.check:
        assert s['fixed']['readings']==866
        assert s['adaptive']['readings']==30
        assert 89<s['adaptive']['message_rate_reduction_pct']<91
        assert s['outage']['recovered_readings']==53
        assert s['outage']['missing_recovered_seq']==[]
        assert s['outage']['duplicate_stored_ids']==s['outage']['dropped_samples']==0
        assert [r['alert'] for r in s['alerts']['transitions']]==[True,False]
        assert s['alerts']['transitions'][0]['temperature_c']>=28
        assert s['alerts']['transitions'][1]['temperature_c']<=27.8
        assert s['alerts']['hysteresis_hold_observed']
    output=json.dumps(s,indent=2)+'\n'
    if args.write:(ROOT/'results/summary.json').write_text(output)
    print(output,end='')

if __name__=='__main__':main()
