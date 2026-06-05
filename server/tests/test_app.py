import base64
from fastapi.testclient import TestClient
from server import app as appmod


def make_client(tmp_path, monkeypatch):
    monkeypatch.setenv("DATA_DIR", str(tmp_path))
    monkeypatch.setenv("PUBLIC_BASE_URL", "http://h:8080")
    return TestClient(appmod.app)


def _basic(password, user="admin"):
    token = base64.b64encode(f"{user}:{password}".encode()).decode()
    return {"Authorization": f"Basic {token}"}


def test_talk_requires_api_key_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("API_KEY", "secret123")
    monkeypatch.setattr(appmod.pipeline, "process_turn", lambda s, sid, a: "http://h/audio/x/0.mp3")
    c = make_client(tmp_path, monkeypatch)
    # без ключа → 401
    r = c.post("/talk?session=x", content=b"\x00\x01")
    assert r.status_code == 401
    # с верным ключом → проходит
    r = c.post("/talk?session=x", content=b"\x00\x01", headers={"X-API-Key": "secret123"})
    assert r.status_code == 200
    # с неверным ключом → 401
    r = c.post("/talk?session=x", content=b"\x00\x01", headers={"X-API-Key": "nope"})
    assert r.status_code == 401


def test_index_requires_password_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("WEB_PASSWORD", "adminpass")
    monkeypatch.setenv("API_KEY", "secret123")
    c = make_client(tmp_path, monkeypatch)
    # без авторизации → 401 + WWW-Authenticate
    r = c.get("/")
    assert r.status_code == 401
    assert "Basic" in r.headers.get("www-authenticate", "")
    # с верным паролем → 200, и ключ встроен в страницу
    r = c.get("/", headers=_basic("adminpass"))
    assert r.status_code == 200
    assert "secret123" in r.text
    assert "__API_KEY__" not in r.text
    # с неверным паролем → 401
    r = c.get("/", headers=_basic("wrong"))
    assert r.status_code == 401


def test_index_served(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/")
    assert r.status_code == 200
    assert "text/html" in r.headers["content-type"]
    assert "/talk" in r.text            # страница знает про эндпоинт
    assert "AudioContext" in r.text     # шлёт сырой LPCM через WebAudio


def test_sessions_list_and_detail(tmp_path, monkeypatch):
    from server import sessions
    sessions.append_turn(tmp_path, "sid-1", "user", "привет", audio="in_0.wav")
    sessions.append_turn(tmp_path, "sid-1", "bot", "здравствуй", audio="0.mp3")
    (tmp_path / "audio" / "sid-1").mkdir(parents=True)
    (tmp_path / "audio" / "sid-1" / "0.mp3").write_bytes(b"M")
    c = make_client(tmp_path, monkeypatch)

    r = c.get("/sessions")
    assert r.status_code == 200
    ids = [s["id"] for s in r.json()["sessions"]]
    assert "sid-1" in ids
    assert next(s for s in r.json()["sessions"] if s["id"] == "sid-1")["audio"] == 1

    r = c.get("/sessions/sid-1")
    assert r.status_code == 200
    body = r.json()
    assert body["turns"] == [
        {"role": "user", "text": "привет", "audio": "in_0.wav"},
        {"role": "bot", "text": "здравствуй", "audio": "0.mp3"}]
    assert body["audio"] == ["0.mp3"]


def test_sessions_require_api_key_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("API_KEY", "secret123")
    c = make_client(tmp_path, monkeypatch)
    assert c.get("/sessions").status_code == 401
    assert c.get("/sessions", headers={"X-API-Key": "secret123"}).status_code == 200
    assert c.get("/sessions/anything", headers={"X-API-Key": "secret123"}).status_code == 200


def test_talk_returns_url(tmp_path, monkeypatch):
    monkeypatch.setattr(appmod.pipeline, "process_turn",
                        lambda s, sid, audio: "http://h:8080/audio/x/0.mp3")
    c = make_client(tmp_path, monkeypatch)
    r = c.post("/talk?session=x", content=b"\x00\x01",
               headers={"Content-Type": "application/octet-stream"})
    assert r.status_code == 200
    assert r.json() == {"url": "http://h:8080/audio/x/0.mp3"}


def test_talk_pipeline_error_returns_502(tmp_path, monkeypatch):
    def boom(s, sid, audio):
        raise RuntimeError("yandex down")
    monkeypatch.setattr(appmod.pipeline, "process_turn", boom)
    c = make_client(tmp_path, monkeypatch)
    r = c.post("/talk?session=x", content=b"\x00\x01")
    assert r.status_code == 502
    assert "yandex down" in r.json()["error"]


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


def test_audio_rejects_encoded_dotdot_session(tmp_path, monkeypatch):
    # %2e%2e декодируется в '..' как ОДИН сегмент session_id → доходит до хэндлера
    # и должен отлетать на проверке регекса (а не на роутинге).
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/audio/%2e%2e/0.mp3")
    assert r.status_code == 400


def test_speedtest_ping_ok(tmp_path, monkeypatch):
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/speedtest/ping")
    assert r.status_code == 200
    assert r.content == b"ok"


def test_speedtest_ping_requires_api_key_when_set(tmp_path, monkeypatch):
    monkeypatch.setenv("API_KEY", "secret123")
    c = make_client(tmp_path, monkeypatch)
    assert c.get("/speedtest/ping").status_code == 401
    assert c.get("/speedtest/ping", headers={"X-API-Key": "secret123"}).status_code == 200
