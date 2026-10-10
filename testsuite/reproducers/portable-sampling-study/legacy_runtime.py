"""Reject historical environment-driven measurements against the new public API."""
from pathlib import Path

def require_legacy_sampling(build):
    library = Path(build) / 'lib/libCoinRender.so'
    if not library.is_file() or b'COIN_SAMPLING_STUDY' not in library.read_bytes():
        raise SystemExit('This runner needs a frozen environment-driven study runtime (commit 43f00b0e). '
                         'This API revision uses explicit target options; use sampling-api/run.py. '
                         'No measurement was started.')
