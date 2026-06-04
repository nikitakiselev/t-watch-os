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
