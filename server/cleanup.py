import shutil
from pathlib import Path


def cleanup_old(data_dir: Path, ttl_seconds: int, now: float) -> None:
    cutoff = now - ttl_seconds
    sessions = data_dir / "sessions"
    if sessions.is_dir():
        for f in sessions.glob("*.txt"):
            if f.stat().st_mtime < cutoff:
                f.unlink(missing_ok=True)
    audio = data_dir / "audio"
    if audio.is_dir():
        for d in audio.iterdir():
            if d.is_dir() and d.stat().st_mtime < cutoff:
                shutil.rmtree(d, ignore_errors=True)
