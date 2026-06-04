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
