"""Check local README targets; external URLs are not requested."""
import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
missing=[]
for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)',(root/'README.md').read_text()):
    target=target.split('#',1)[0]
    if not target or '://' in target or target.startswith('mailto:'):continue
    if not (root/target).exists():missing.append(target)
if missing:raise SystemExit('Broken README targets: '+', '.join(missing))
print('README local link targets: PASS')
