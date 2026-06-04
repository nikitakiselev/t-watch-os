import re
from pathlib import Path

_SAFE = re.compile(r"[^A-Za-z0-9_-]")
_NEWLINES = re.compile(r"[\r\n]+")


def _safe_id(session_id: str) -> str:
    # UUID приходит снаружи — не пускаем его в путь без чистки.
    return _SAFE.sub("", session_id)[:64] or "default"


def _sessions_dir(data_dir: Path) -> Path:
    return data_dir / "sessions"


def _history_file(data_dir: Path, session_id: str) -> Path:
    return _sessions_dir(data_dir) / f"{_safe_id(session_id)}.txt"


def append_turn(data_dir: Path, session_id: str, role: str, text: str) -> None:
    # Схлопываем любые переводы строк (\r, \n, \r\n) в пробел — одна реплика = одна строка.
    line = f"{role}: {_NEWLINES.sub(' ', text).strip()}\n"
    _sessions_dir(data_dir).mkdir(parents=True, exist_ok=True)
    with _history_file(data_dir, session_id).open("a", encoding="utf-8") as f:
        f.write(line)


def read_history(data_dir: Path, session_id: str) -> list[tuple[str, str]]:
    path = _history_file(data_dir, session_id)
    if not path.exists():
        return []
    out: list[tuple[str, str]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if ": " in raw:
            role, text = raw.split(": ", 1)
            out.append((role, text))
    return out


def audio_dir(data_dir: Path, session_id: str) -> Path:
    d = data_dir / "audio" / _safe_id(session_id)
    d.mkdir(parents=True, exist_ok=True)
    return d


def next_audio_path(data_dir: Path, session_id: str) -> Path:
    d = audio_dir(data_dir, session_id)
    n = len(list(d.glob("*.mp3")))
    return d / f"{n}.mp3"
