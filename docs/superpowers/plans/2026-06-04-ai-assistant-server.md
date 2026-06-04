# AI Assistant — Сервер (Фаза 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Python-сервер голосового ассистента: принимает аудио + UUID сессии, делает STT → YandexGPT (с историей) → TTS, возвращает URL mp3-ответа.

**Architecture:** FastAPI-сервис в Docker. Эндпоинт `POST /talk?session=UUID` принимает сырой LPCM 16 кГц моно, прогоняет конвейер (Yandex SpeechKit STT → дозапись истории в txt → YandexGPT → дозапись → Yandex TTS oggopus → ffmpeg в mp3 → сохранение файла) и отдаёт `{"url": ...}`. `GET /audio/UUID/N.mp3` раздаёт файл. Фоновая чистка сессий по TTL (эфемерность). Все внешние вызовы (Yandex, ffmpeg) изолированы в отдельных модулях и мокаются в тестах.

**Tech Stack:** Python 3.11, FastAPI, uvicorn, httpx (вызовы Yandex), pytest, ffmpeg (транскод oggopus→mp3), Docker / docker compose.

> **Уточнение протокола относительно спеки:** часы шлют **сырой LPCM** 16 кГц/16 бит/моно (а не WAV) — это нативный формат Yandex STT (`format=lpcm`), ноль конвертаций. Ответ — **mp3** (Yandex TTS отдаёт oggopus → транскодим ffmpeg в mp3, т.к. mp3 надёжнее всего тянет `ESP32-audioI2S`). Фаза 2 (прошивка) подстроится под этот протокол.

---

## Структура файлов

Всё в каталоге `server/`:

- `server/config.py` — настройки из env (`Settings`, `get_settings`).
- `server/sessions.py` — пути сессии, дозапись/чтение истории, нумерация аудио.
- `server/yandex.py` — клиенты Yandex SpeechKit: `stt_recognize`, `gpt_complete`, `tts_synthesize`.
- `server/audio.py` — `opus_to_mp3` (транскод через ffmpeg).
- `server/pipeline.py` — `process_turn`: оркестрация одного хода диалога.
- `server/cleanup.py` — `cleanup_old`: удаление сессий/аудио старше TTL.
- `server/app.py` — FastAPI-приложение, роуты `/talk`, `/audio/...`, фоновая чистка.
- `server/tests/` — pytest-тесты (по файлу на модуль).
- `server/requirements.txt`, `server/Dockerfile`, `docker-compose.yml`, `server/.env.example`.

---

## Task 1: Каркас проекта и конфиг

**Files:**
- Create: `server/requirements.txt`
- Create: `server/config.py`
- Create: `server/tests/__init__.py`
- Create: `server/tests/test_config.py`

- [ ] **Step 1: requirements.txt**

```
fastapi==0.115.0
uvicorn[standard]==0.30.6
httpx==0.27.2
pytest==8.3.3
```

- [ ] **Step 2: Создать venv и поставить зависимости**

Run: `cd server && python3 -m venv .venv && . .venv/bin/activate && pip install -r requirements.txt`
Expected: установка без ошибок.

- [ ] **Step 3: Написать падающий тест конфига**

`server/tests/test_config.py`:
```python
import importlib
from server import config

def test_settings_reads_env(monkeypatch):
    monkeypatch.setenv("YANDEX_API_KEY", "key123")
    monkeypatch.setenv("YANDEX_FOLDER_ID", "folder123")
    monkeypatch.setenv("PUBLIC_BASE_URL", "http://192.168.0.5:8080")
    monkeypatch.setenv("SESSION_TTL", "3600")
    importlib.reload(config)
    s = config.get_settings()
    assert s.api_key == "key123"
    assert s.folder_id == "folder123"
    assert s.public_base_url == "http://192.168.0.5:8080"
    assert s.session_ttl == 3600

def test_settings_defaults(monkeypatch):
    monkeypatch.delenv("SESSION_TTL", raising=False)
    importlib.reload(config)
    s = config.get_settings()
    assert s.session_ttl == 86400
    assert s.data_dir.name == "data"
```

- [ ] **Step 4: Запустить — убедиться, что падает**

Run: `cd server && python -m pytest tests/test_config.py -v`
Expected: FAIL (`ModuleNotFoundError: No module named 'server.config'` или AttributeError).

- [ ] **Step 5: Реализовать config.py**

`server/config.py`:
```python
import os
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Settings:
    api_key: str = field(default_factory=lambda: os.getenv("YANDEX_API_KEY", ""))
    folder_id: str = field(default_factory=lambda: os.getenv("YANDEX_FOLDER_ID", ""))
    public_base_url: str = field(
        default_factory=lambda: os.getenv("PUBLIC_BASE_URL", "http://localhost:8080"))
    session_ttl: int = field(
        default_factory=lambda: int(os.getenv("SESSION_TTL", "86400")))
    data_dir: Path = field(
        default_factory=lambda: Path(os.getenv("DATA_DIR", "data")))
    voice: str = field(default_factory=lambda: os.getenv("YANDEX_VOICE", "alena"))
    system_prompt: str = field(default_factory=lambda: os.getenv(
        "SYSTEM_PROMPT",
        "Ты голосовой ассистент в наручных часах. Отвечай очень кратко, "
        "разговорно, одной-двумя фразами, без списков и markdown."))
    max_tokens: int = field(default_factory=lambda: int(os.getenv("MAX_TOKENS", "120")))


def get_settings() -> Settings:
    return Settings()
```

- [ ] **Step 6: Создать пустой `server/tests/__init__.py` и `server/__init__.py`**

Run: `cd server && touch __init__.py tests/__init__.py`

- [ ] **Step 7: Запустить — убедиться, что проходит**

Run: `cd server && python -m pytest tests/test_config.py -v`
Expected: PASS (2 passed). Запускать pytest из каталога `server`'s parent? Нет — из `server/`, импорт `from server import config` требует, чтобы корень репо был на `sys.path`. Добавь `server/conftest.py` (Step 8).

- [ ] **Step 8: Добавить conftest для импорта пакета**

`server/conftest.py`:
```python
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
```

Run снова: `cd server && python -m pytest tests/test_config.py -v` → Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add server/requirements.txt server/config.py server/__init__.py server/conftest.py server/tests/__init__.py server/tests/test_config.py
git commit -m "feat(server): каркас проекта и конфиг из env"
```

---

## Task 2: Хранилище сессий (история + пути аудио)

**Files:**
- Create: `server/sessions.py`
- Create: `server/tests/test_sessions.py`

- [ ] **Step 1: Падающий тест**

`server/tests/test_sessions.py`:
```python
from server import sessions

def test_append_and_read_history(tmp_path):
    sid = "abc-123"
    sessions.append_turn(tmp_path, sid, "user", "привет")
    sessions.append_turn(tmp_path, sid, "bot", "здравствуй")
    hist = sessions.read_history(tmp_path, sid)
    assert hist == [("user", "привет"), ("bot", "здравствуй")]

def test_read_history_missing_session(tmp_path):
    assert sessions.read_history(tmp_path, "nope") == []

def test_history_survives_newlines_in_text(tmp_path):
    sid = "s1"
    sessions.append_turn(tmp_path, sid, "user", "строка1\nстрока2")
    hist = sessions.read_history(tmp_path, sid)
    assert hist == [("user", "строка1 строка2")]

def test_next_audio_path_increments(tmp_path):
    sid = "s2"
    p1 = sessions.next_audio_path(tmp_path, sid)
    p1.write_bytes(b"x")
    p2 = sessions.next_audio_path(tmp_path, sid)
    assert p1.name == "0.mp3"
    assert p2.name == "1.mp3"
    assert p1.parent == p2.parent
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_sessions.py -v`
Expected: FAIL (нет модуля `sessions`).

- [ ] **Step 3: Реализовать sessions.py**

`server/sessions.py`:
```python
import re
from pathlib import Path

_SAFE = re.compile(r"[^A-Za-z0-9_-]")


def _safe_id(session_id: str) -> str:
    # UUID приходит снаружи — не пускаем его в путь без чистки.
    return _SAFE.sub("", session_id)[:64] or "default"


def _history_file(data_dir: Path, session_id: str) -> Path:
    d = data_dir / "sessions"
    d.mkdir(parents=True, exist_ok=True)
    return d / f"{_safe_id(session_id)}.txt"


def append_turn(data_dir: Path, session_id: str, role: str, text: str) -> None:
    line = f"{role}: {text.replace(chr(10), ' ').strip()}\n"
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
```

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_sessions.py -v`
Expected: PASS (4 passed).

- [ ] **Step 5: Commit**

```bash
git add server/sessions.py server/tests/test_sessions.py
git commit -m "feat(server): хранилище сессий — история и пути аудио"
```

---

## Task 3: Клиент Yandex STT

**Files:**
- Create: `server/yandex.py`
- Create: `server/tests/test_yandex_stt.py`

- [ ] **Step 1: Падающий тест (мокаем httpx)**

`server/tests/test_yandex_stt.py`:
```python
import httpx
from server import yandex

class FakeResp:
    def __init__(self, status, json_data=None, content=b""):
        self.status_code = status
        self._json = json_data
        self.content = content
    def json(self): return self._json
    def raise_for_status(self):
        if self.status_code >= 400:
            raise httpx.HTTPStatusError("err", request=None, response=None)

def test_stt_recognize_builds_request(monkeypatch):
    captured = {}
    def fake_post(url, *, params, headers, content, timeout):
        captured["url"] = url
        captured["params"] = params
        captured["headers"] = headers
        captured["content"] = content
        return FakeResp(200, {"result": "привет мир"})
    monkeypatch.setattr(yandex.httpx, "post", fake_post)
    text = yandex.stt_recognize(b"\x01\x02", api_key="K", folder_id="F", sample_rate=16000)
    assert text == "привет мир"
    assert "stt.api.cloud.yandex.net" in captured["url"]
    assert captured["params"]["format"] == "lpcm"
    assert captured["params"]["sampleRateHertz"] == 16000
    assert captured["params"]["folderId"] == "F"
    assert captured["headers"]["Authorization"] == "Api-Key K"
    assert captured["content"] == b"\x01\x02"
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_yandex_stt.py -v`
Expected: FAIL (нет `yandex` / нет `stt_recognize`).

- [ ] **Step 3: Реализовать STT в yandex.py**

`server/yandex.py`:
```python
import httpx

_TIMEOUT = 20.0
_STT_URL = "https://stt.api.cloud.yandex.net/speech/v1/stt:recognize"


def stt_recognize(audio_lpcm: bytes, *, api_key: str, folder_id: str,
                  sample_rate: int = 16000, lang: str = "ru-RU") -> str:
    resp = httpx.post(
        _STT_URL,
        params={"folderId": folder_id, "lang": lang,
                "format": "lpcm", "sampleRateHertz": sample_rate},
        headers={"Authorization": f"Api-Key {api_key}"},
        content=audio_lpcm,
        timeout=_TIMEOUT,
    )
    resp.raise_for_status()
    return resp.json().get("result", "")
```

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_yandex_stt.py -v`
Expected: PASS (1 passed).

- [ ] **Step 5: Commit**

```bash
git add server/yandex.py server/tests/test_yandex_stt.py
git commit -m "feat(server): клиент Yandex SpeechKit STT"
```

---

## Task 4: Клиент YandexGPT

**Files:**
- Modify: `server/yandex.py`
- Create: `server/tests/test_yandex_gpt.py`

- [ ] **Step 1: Падающий тест**

`server/tests/test_yandex_gpt.py`:
```python
from server import yandex
from server.tests.test_yandex_stt import FakeResp

def test_gpt_complete_builds_request(monkeypatch):
    captured = {}
    def fake_post(url, *, json, headers, timeout):
        captured["url"] = url
        captured["json"] = json
        captured["headers"] = headers
        return FakeResp(200, {"result": {"alternatives": [
            {"message": {"role": "assistant", "text": "ответ"}}]}})
    monkeypatch.setattr(yandex.httpx, "post", fake_post)
    msgs = [{"role": "system", "text": "будь краток"},
            {"role": "user", "text": "привет"}]
    out = yandex.gpt_complete(msgs, api_key="K", folder_id="F", max_tokens=50)
    assert out == "ответ"
    assert "foundationModels" in captured["url"]
    assert captured["json"]["modelUri"] == "gpt://F/yandexgpt-lite/latest"
    assert captured["json"]["messages"] == msgs
    assert captured["json"]["completionOptions"]["maxTokens"] == "50"
    assert captured["headers"]["Authorization"] == "Api-Key K"
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_yandex_gpt.py -v`
Expected: FAIL (нет `gpt_complete`).

- [ ] **Step 3: Дописать GPT в yandex.py**

Добавить в `server/yandex.py`:
```python
_GPT_URL = "https://llm.api.cloud.yandex.net/foundationModels/v1/completion"


def gpt_complete(messages: list[dict], *, api_key: str, folder_id: str,
                 max_tokens: int = 120, temperature: float = 0.6) -> str:
    resp = httpx.post(
        _GPT_URL,
        json={
            "modelUri": f"gpt://{folder_id}/yandexgpt-lite/latest",
            "completionOptions": {"stream": False,
                                  "temperature": temperature,
                                  "maxTokens": str(max_tokens)},
            "messages": messages,
        },
        headers={"Authorization": f"Api-Key {api_key}"},
        timeout=_TIMEOUT,
    )
    resp.raise_for_status()
    alts = resp.json()["result"]["alternatives"]
    return alts[0]["message"]["text"] if alts else ""
```

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_yandex_gpt.py -v`
Expected: PASS (1 passed).

- [ ] **Step 5: Commit**

```bash
git add server/yandex.py server/tests/test_yandex_gpt.py
git commit -m "feat(server): клиент YandexGPT (completion)"
```

---

## Task 5: Клиент Yandex TTS

**Files:**
- Modify: `server/yandex.py`
- Create: `server/tests/test_yandex_tts.py`

- [ ] **Step 1: Падающий тест**

`server/tests/test_yandex_tts.py`:
```python
from server import yandex
from server.tests.test_yandex_stt import FakeResp

def test_tts_synthesize_builds_request(monkeypatch):
    captured = {}
    def fake_post(url, *, data, headers, timeout):
        captured["url"] = url
        captured["data"] = data
        captured["headers"] = headers
        return FakeResp(200, content=b"OGGoctets")
    monkeypatch.setattr(yandex.httpx, "post", fake_post)
    audio = yandex.tts_synthesize("привет", api_key="K", folder_id="F", voice="alena")
    assert audio == b"OGGoctets"
    assert "tts.api.cloud.yandex.net" in captured["url"]
    assert captured["data"]["text"] == "привет"
    assert captured["data"]["format"] == "oggopus"
    assert captured["data"]["voice"] == "alena"
    assert captured["data"]["folderId"] == "F"
    assert captured["headers"]["Authorization"] == "Api-Key K"
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_yandex_tts.py -v`
Expected: FAIL (нет `tts_synthesize`).

- [ ] **Step 3: Дописать TTS в yandex.py**

Добавить в `server/yandex.py`:
```python
_TTS_URL = "https://tts.api.cloud.yandex.net/speech/v1/tts:synthesize"


def tts_synthesize(text: str, *, api_key: str, folder_id: str,
                   voice: str = "alena", lang: str = "ru-RU") -> bytes:
    resp = httpx.post(
        _TTS_URL,
        data={"text": text, "lang": lang, "voice": voice,
              "format": "oggopus", "folderId": folder_id},
        headers={"Authorization": f"Api-Key {api_key}"},
        timeout=_TIMEOUT,
    )
    resp.raise_for_status()
    return resp.content
```

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_yandex_tts.py -v`
Expected: PASS (1 passed).

- [ ] **Step 5: Commit**

```bash
git add server/yandex.py server/tests/test_yandex_tts.py
git commit -m "feat(server): клиент Yandex SpeechKit TTS (oggopus)"
```

---

## Task 6: Транскод oggopus → mp3 (ffmpeg)

**Files:**
- Create: `server/audio.py`
- Create: `server/tests/test_audio.py`

- [ ] **Step 1: Падающий тест (мокаем subprocess)**

`server/tests/test_audio.py`:
```python
from server import audio

def test_opus_to_mp3_invokes_ffmpeg(monkeypatch):
    captured = {}
    class FakeProc:
        returncode = 0
        def communicate(self, input=None):
            captured["input"] = input
            return (b"ID3mp3bytes", b"")
    def fake_popen(cmd, stdin, stdout, stderr):
        captured["cmd"] = cmd
        return FakeProc()
    monkeypatch.setattr(audio.subprocess, "Popen", fake_popen)
    out = audio.opus_to_mp3(b"OGGoctets")
    assert out == b"ID3mp3bytes"
    assert captured["input"] == b"OGGoctets"
    assert "ffmpeg" in captured["cmd"][0]
    assert "mp3" in captured["cmd"]
    # читаем из stdin (pipe:0), пишем в stdout (pipe:1)
    assert "pipe:0" in captured["cmd"]
    assert "pipe:1" in captured["cmd"]

def test_opus_to_mp3_raises_on_ffmpeg_error(monkeypatch):
    class FakeProc:
        returncode = 1
        def communicate(self, input=None):
            return (b"", b"boom")
    monkeypatch.setattr(audio.subprocess, "Popen",
                        lambda *a, **k: FakeProc())
    try:
        audio.opus_to_mp3(b"x")
        assert False, "ожидали RuntimeError"
    except RuntimeError as e:
        assert "ffmpeg" in str(e)
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_audio.py -v`
Expected: FAIL (нет `audio`).

- [ ] **Step 3: Реализовать audio.py**

`server/audio.py`:
```python
import subprocess


def opus_to_mp3(opus: bytes) -> bytes:
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error",
           "-i", "pipe:0", "-f", "mp3", "-codec:a", "libmp3lame",
           "-q:a", "5", "pipe:1"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate(input=opus)
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg failed: {err.decode('utf-8', 'replace')}")
    return out
```

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_audio.py -v`
Expected: PASS (2 passed).

- [ ] **Step 5: Commit**

```bash
git add server/audio.py server/tests/test_audio.py
git commit -m "feat(server): транскод oggopus->mp3 через ffmpeg"
```

---

## Task 7: Конвейер хода диалога

**Files:**
- Create: `server/pipeline.py`
- Create: `server/tests/test_pipeline.py`

- [ ] **Step 1: Падающий тест (мокаем yandex + audio)**

`server/tests/test_pipeline.py`:
```python
from server import pipeline, sessions
from server.config import Settings

def test_process_turn_full_flow(tmp_path, monkeypatch):
    s = Settings(api_key="K", folder_id="F", public_base_url="http://h:8080",
                 data_dir=tmp_path, system_prompt="будь краток", max_tokens=50,
                 voice="alena")
    monkeypatch.setattr(pipeline.yandex, "stt_recognize",
                        lambda audio, **kw: "как дела")
    seen = {}
    def fake_gpt(messages, **kw):
        seen["messages"] = messages
        return "нормально"
    monkeypatch.setattr(pipeline.yandex, "gpt_complete", fake_gpt)
    monkeypatch.setattr(pipeline.yandex, "tts_synthesize",
                        lambda text, **kw: b"OPUS")
    monkeypatch.setattr(pipeline.audio, "opus_to_mp3", lambda b: b"MP3")

    url = pipeline.process_turn(s, "sid-1", b"\x00\x01")

    # GPT получил system + историю с текущей репликой пользователя
    assert seen["messages"][0] == {"role": "system", "text": "будь краток"}
    assert seen["messages"][-1] == {"role": "user", "text": "как дела"}
    # история записана (user + bot)
    assert sessions.read_history(tmp_path, "sid-1") == [
        ("user", "как дела"), ("bot", "нормально")]
    # mp3 сохранён и URL указывает на него
    assert url == "http://h:8080/audio/sid-1/0.mp3"
    saved = (tmp_path / "audio" / "sid-1" / "0.mp3").read_bytes()
    assert saved == b"MP3"

def test_process_turn_includes_prior_context(tmp_path, monkeypatch):
    s = Settings(api_key="K", folder_id="F", public_base_url="http://h:8080",
                 data_dir=tmp_path, system_prompt="sys", max_tokens=50)
    sessions.append_turn(tmp_path, "sid-2", "user", "меня зовут Кит")
    sessions.append_turn(tmp_path, "sid-2", "bot", "приятно")
    monkeypatch.setattr(pipeline.yandex, "stt_recognize", lambda a, **k: "как меня зовут")
    seen = {}
    monkeypatch.setattr(pipeline.yandex, "gpt_complete",
                        lambda m, **k: seen.setdefault("m", m) or "Кит")
    monkeypatch.setattr(pipeline.yandex, "tts_synthesize", lambda t, **k: b"O")
    monkeypatch.setattr(pipeline.audio, "opus_to_mp3", lambda b: b"M")
    pipeline.process_turn(s, "sid-2", b"x")
    roles_texts = [(m["role"], m["text"]) for m in seen["m"]]
    assert ("user", "меня зовут Кит") in roles_texts
    assert ("assistant", "приятно") in roles_texts

def test_process_turn_empty_stt_skips(tmp_path, monkeypatch):
    s = Settings(data_dir=tmp_path)
    monkeypatch.setattr(pipeline.yandex, "stt_recognize", lambda a, **k: "   ")
    url = pipeline.process_turn(s, "sid-3", b"x")
    assert url is None
    assert sessions.read_history(tmp_path, "sid-3") == []
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_pipeline.py -v`
Expected: FAIL (нет `pipeline`).

- [ ] **Step 3: Реализовать pipeline.py**

`server/pipeline.py`:
```python
from server import audio, sessions, yandex
from server.config import Settings

_ROLE_MAP = {"user": "user", "bot": "assistant"}


def _build_messages(settings: Settings, history: list[tuple[str, str]]) -> list[dict]:
    msgs = [{"role": "system", "text": settings.system_prompt}]
    for role, text in history:
        msgs.append({"role": _ROLE_MAP.get(role, "user"), "text": text})
    return msgs


def process_turn(settings: Settings, session_id: str, audio_lpcm: bytes):
    user_text = yandex.stt_recognize(
        audio_lpcm, api_key=settings.api_key, folder_id=settings.folder_id).strip()
    if not user_text:
        return None

    sessions.append_turn(settings.data_dir, session_id, "user", user_text)
    history = sessions.read_history(settings.data_dir, session_id)
    messages = _build_messages(settings, history)

    reply = yandex.gpt_complete(
        messages, api_key=settings.api_key, folder_id=settings.folder_id,
        max_tokens=settings.max_tokens).strip()
    sessions.append_turn(settings.data_dir, session_id, "bot", reply)

    opus = yandex.tts_synthesize(
        reply, api_key=settings.api_key, folder_id=settings.folder_id,
        voice=settings.voice)
    mp3 = audio.opus_to_mp3(opus)

    path = sessions.next_audio_path(settings.data_dir, session_id)
    path.write_bytes(mp3)
    return f"{settings.public_base_url}/audio/{session_id}/{path.name}"
```

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_pipeline.py -v`
Expected: PASS (3 passed).

- [ ] **Step 5: Commit**

```bash
git add server/pipeline.py server/tests/test_pipeline.py
git commit -m "feat(server): конвейер хода диалога STT->GPT->TTS->mp3"
```

---

## Task 8: HTTP-эндпоинты (FastAPI)

**Files:**
- Create: `server/app.py`
- Create: `server/tests/test_app.py`

- [ ] **Step 1: Падающий тест (мокаем pipeline)**

`server/tests/test_app.py`:
```python
from fastapi.testclient import TestClient
from server import app as appmod

def make_client(tmp_path, monkeypatch):
    monkeypatch.setenv("DATA_DIR", str(tmp_path))
    monkeypatch.setenv("PUBLIC_BASE_URL", "http://h:8080")
    return TestClient(appmod.app)

def test_talk_returns_url(tmp_path, monkeypatch):
    monkeypatch.setattr(appmod.pipeline, "process_turn",
                        lambda s, sid, audio: "http://h:8080/audio/x/0.mp3")
    c = make_client(tmp_path, monkeypatch)
    r = c.post("/talk?session=x", content=b"\x00\x01",
               headers={"Content-Type": "application/octet-stream"})
    assert r.status_code == 200
    assert r.json() == {"url": "http://h:8080/audio/x/0.mp3"}

def test_talk_empty_recording_returns_204(tmp_path, monkeypatch):
    monkeypatch.setattr(appmod.pipeline, "process_turn", lambda s, sid, audio: None)
    c = make_client(tmp_path, monkeypatch)
    r = c.post("/talk?session=x", content=b"")
    assert r.status_code == 204

def test_talk_requires_session(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    r = c.post("/talk", content=b"x")
    assert r.status_code == 422

def test_audio_serves_file(tmp_path, monkeypatch):
    d = tmp_path / "audio" / "sid"
    d.mkdir(parents=True)
    (d / "0.mp3").write_bytes(b"MP3DATA")
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/audio/sid/0.mp3")
    assert r.status_code == 200
    assert r.content == b"MP3DATA"
    assert r.headers["content-type"].startswith("audio/")

def test_audio_404_for_missing(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/audio/sid/9.mp3")
    assert r.status_code == 404

def test_audio_rejects_traversal(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/audio/sid/..%2f..%2fconfig.py")
    assert r.status_code in (400, 404)
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_app.py -v`
Expected: FAIL (нет `app`).

- [ ] **Step 3: Реализовать app.py**

`server/app.py`:
```python
import re
from fastapi import FastAPI, Request, Response, HTTPException
from fastapi.responses import JSONResponse, FileResponse

from server import pipeline
from server.config import get_settings

app = FastAPI(title="watchos AI assistant")

_SAFE = re.compile(r"^[A-Za-z0-9_-]+$")


@app.post("/talk")
async def talk(session: str, request: Request):
    settings = get_settings()
    audio_lpcm = await request.body()
    url = pipeline.process_turn(settings, session, audio_lpcm)
    if url is None:
        return Response(status_code=204)
    return JSONResponse({"url": url})


@app.get("/audio/{session_id}/{name}")
def audio(session_id: str, name: str):
    settings = get_settings()
    if not _SAFE.match(session_id) or not _SAFE.match(name.replace(".", "")):
        raise HTTPException(status_code=400, detail="bad path")
    path = settings.data_dir / "audio" / session_id / name
    if not path.is_file():
        raise HTTPException(status_code=404, detail="not found")
    return FileResponse(path, media_type="audio/mpeg")
```

Примечание: `session` как query-параметр у `talk` обязателен → FastAPI вернёт 422 при отсутствии (покрывает `test_talk_requires_session`).

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_app.py -v`
Expected: PASS (6 passed).

- [ ] **Step 5: Commit**

```bash
git add server/app.py server/tests/test_app.py
git commit -m "feat(server): эндпоинты /talk и /audio"
```

---

## Task 9: Чистка сессий по TTL

**Files:**
- Create: `server/cleanup.py`
- Create: `server/tests/test_cleanup.py`
- Modify: `server/app.py`

- [ ] **Step 1: Падающий тест**

`server/tests/test_cleanup.py`:
```python
import os, time
from server import cleanup

def test_cleanup_removes_old_and_keeps_fresh(tmp_path):
    sess = tmp_path / "sessions"; sess.mkdir()
    aud = tmp_path / "audio" / "old"; aud.mkdir(parents=True)
    old_txt = sess / "old.txt"; old_txt.write_text("x")
    old_mp3 = aud / "0.mp3"; old_mp3.write_bytes(b"x")
    fresh = sess / "fresh.txt"; fresh.write_text("y")

    old_time = time.time() - 10_000
    os.utime(old_txt, (old_time, old_time))
    os.utime(old_mp3, (old_time, old_time))
    os.utime(aud, (old_time, old_time))

    cleanup.cleanup_old(tmp_path, ttl_seconds=3600, now=time.time())

    assert not old_txt.exists()
    assert not aud.exists()        # пустой каталог сессии тоже убран
    assert fresh.exists()
```

- [ ] **Step 2: Запустить — падает**

Run: `cd server && python -m pytest tests/test_cleanup.py -v`
Expected: FAIL (нет `cleanup`).

- [ ] **Step 3: Реализовать cleanup.py**

`server/cleanup.py`:
```python
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
```

- [ ] **Step 4: Запустить — проходит**

Run: `cd server && python -m pytest tests/test_cleanup.py -v`
Expected: PASS (1 passed).

- [ ] **Step 5: Подключить периодическую чистку в app.py**

Добавить в `server/app.py` (импорты сверху и стартовый хук):
```python
import asyncio, time
from server import cleanup


@app.on_event("startup")
async def _start_cleanup():
    async def loop():
        while True:
            s = get_settings()
            cleanup.cleanup_old(s.data_dir, s.session_ttl, time.time())
            await asyncio.sleep(3600)
    asyncio.create_task(loop())
```

- [ ] **Step 6: Прогнать весь набор тестов**

Run: `cd server && python -m pytest -v`
Expected: PASS (все тесты зелёные; стартовый хук в TestClient запускается, но не мешает).

- [ ] **Step 7: Commit**

```bash
git add server/cleanup.py server/tests/test_cleanup.py server/app.py
git commit -m "feat(server): фоновая чистка сессий по TTL"
```

---

## Task 10: Docker, compose и запуск

**Files:**
- Create: `server/Dockerfile`
- Create: `server/.env.example`
- Create: `docker-compose.yml`
- Create: `server/README.md`

- [ ] **Step 1: Dockerfile (с ffmpeg)**

`server/Dockerfile`:
```dockerfile
FROM python:3.11-slim
RUN apt-get update && apt-get install -y --no-install-recommends ffmpeg \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY . /app/server
ENV DATA_DIR=/app/data
EXPOSE 8080
CMD ["uvicorn", "server.app:app", "--host", "0.0.0.0", "--port", "8080"]
```

- [ ] **Step 2: .env.example**

`server/.env.example`:
```
YANDEX_API_KEY=replace-me
YANDEX_FOLDER_ID=replace-me
# LAN-адрес сервера, по которому ЧАСЫ скачают аудио (не localhost!):
PUBLIC_BASE_URL=http://192.168.0.5:8080
SESSION_TTL=86400
YANDEX_VOICE=alena
MAX_TOKENS=120
```

- [ ] **Step 3: docker-compose.yml (корень репо)**

`docker-compose.yml`:
```yaml
services:
  assistant:
    build: ./server
    ports:
      - "8080:8080"
    env_file:
      - ./server/.env
    volumes:
      - ./server/data:/app/data
    restart: unless-stopped
```

- [ ] **Step 4: README с инструкцией**

`server/README.md`:
```markdown
# watchos AI assistant — сервер

## Запуск
1. `cp server/.env.example server/.env` и заполнить ключи Yandex + `PUBLIC_BASE_URL`
   (LAN-IP машины, видимый часам — узнать через `ipconfig getifaddr en0`).
2. `docker compose up --build`
3. Проверка с тестовым LPCM:
   `ffmpeg -i sample.wav -f s16le -ar 16000 -ac 1 sample.pcm`
   `curl -s --data-binary @sample.pcm "http://localhost:8080/talk?session=test" | jq`
   → `{"url": "http://<ip>:8080/audio/test/0.mp3"}`; открыть URL — играет ответ.

## Тесты
`cd server && python -m venv .venv && . .venv/bin/activate && pip install -r requirements.txt && python -m pytest -v`
```

- [ ] **Step 5: Ручная проверка сборки**

Run: `docker compose build`
Expected: образ собирается (ffmpeg ставится), без ошибок.

- [ ] **Step 6: Commit**

```bash
git add server/Dockerfile server/.env.example docker-compose.yml server/README.md
git commit -m "feat(server): docker, compose и инструкция запуска"
```

---

## Финальная проверка фазы

- [ ] Весь набор тестов зелёный: `cd server && python -m pytest -v`
- [ ] `docker compose build` проходит.
- [ ] (С реальными ключами Yandex) сквозной тест из README отдаёт mp3, который играется.
- [ ] `.env` добавлен в `.gitignore` (не коммитить ключи).
