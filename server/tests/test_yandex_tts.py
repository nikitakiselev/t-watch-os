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
