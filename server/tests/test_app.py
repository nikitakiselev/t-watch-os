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


def test_audio_rejects_encoded_dotdot_session(tmp_path, monkeypatch):
    # %2e%2e декодируется в '..' как ОДИН сегмент session_id → доходит до хэндлера
    # и должен отлетать на проверке регекса (а не на роутинге).
    c = make_client(tmp_path, monkeypatch)
    r = c.get("/audio/%2e%2e/0.mp3")
    assert r.status_code == 400
