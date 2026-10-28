"""Clean legacy encoder inspection history at startup."""
import re
from common import STATE, read_json
HISTORY = STATE / "inspection-results"

def cleanup_encoder_history():
    # Startup only, before callback dispatch or inspection workers are started.
    for path in HISTORY.glob('*.json'):
        if re.fullmatch('[0-9a-f]{32}\.json',path.name) and read_json(path).get('kind')=='encoder':
            path.unlink(missing_ok=True)
            for receipt in HISTORY.glob(path.stem+'.*.delivery'):
                receipt.unlink(missing_ok=True)
    # Atomic staging files are uncommitted and have no active writers at startup.
    for path in HISTORY.glob('*.new'):
        path.unlink(missing_ok=True)
