import json
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


def append_turn(data_dir: Path, session_id: str, role: str, text: str,
                audio: str = "") -> None:
    # Одна реплика = одна JSON-строка {role, text, audio}. Переводы строк в тексте
    # схлопываем (одна строка на ход), audio — имя связанного файла (in_N.wav / N.mp3).
    obj = {"role": role, "text": _NEWLINES.sub(" ", text).strip(), "audio": audio}
    _sessions_dir(data_dir).mkdir(parents=True, exist_ok=True)
    with _history_file(data_dir, session_id).open("a", encoding="utf-8") as f:
        f.write(json.dumps(obj, ensure_ascii=False) + "\n")


def read_history(data_dir: Path, session_id: str) -> list[dict]:
    path = _history_file(data_dir, session_id)
    if not path.exists():
        return []
    out: list[dict] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        raw = raw.strip()
        if not raw:
            continue
        try:
            o = json.loads(raw)
            out.append({"role": o.get("role", ""), "text": o.get("text", ""),
                        "audio": o.get("audio", "")})
        except ValueError:
            if ": " in raw:                      # старый формат «role: text» (без аудио)
                role, text = raw.split(": ", 1)
                out.append({"role": role, "text": text, "audio": ""})
    return out


def audio_dir(data_dir: Path, session_id: str) -> Path:
    d = data_dir / "audio" / _safe_id(session_id)
    d.mkdir(parents=True, exist_ok=True)
    return d


def next_audio_path(data_dir: Path, session_id: str) -> Path:
    # Ответ бота: N.mp3 (N — число уже сохранённых mp3).
    d = audio_dir(data_dir, session_id)
    n = len(list(d.glob("*.mp3")))
    return d / f"{n}.mp3"


def next_user_audio_path(data_dir: Path, session_id: str) -> Path:
    # Входящая запись с микрофона: in_N.wav (lossless).
    d = audio_dir(data_dir, session_id)
    n = len(list(d.glob("in_*.wav")))
    return d / f"in_{n}.wav"


def list_audio(data_dir: Path, session_id: str) -> list[str]:
    # И входящие wav, и ответные mp3 — по времени создания (хронологический порядок).
    d = data_dir / "audio" / _safe_id(session_id)
    if not d.is_dir():
        return []
    files = [p for p in d.iterdir() if p.suffix in (".mp3", ".wav")]
    return [p.name for p in sorted(files, key=lambda p: p.stat().st_mtime)]


def list_sessions(data_dir: Path) -> list[dict]:
    d = _sessions_dir(data_dir)
    if not d.is_dir():
        return []
    files = sorted(d.glob("*.txt"), key=lambda p: p.stat().st_mtime, reverse=True)
    out = []
    for f in files:
        sid = f.stem
        out.append({"id": sid,
                    "audio": len(list_audio(data_dir, sid)),
                    "mtime": f.stat().st_mtime})
    return out
