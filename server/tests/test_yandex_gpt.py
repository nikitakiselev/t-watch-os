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
    assert captured["json"]["modelUri"] == "gpt://F/yandexgpt/latest"   # по умолчанию полная модель
    assert captured["json"]["messages"] == msgs
    assert captured["json"]["completionOptions"]["maxTokens"] == "50"
    assert captured["headers"]["Authorization"] == "Api-Key K"


def test_gpt_complete_uses_model_override(monkeypatch):
    captured = {}
    def fake_post(url, *, json, headers, timeout):
        captured["json"] = json
        return FakeResp(200, {"result": {"alternatives": [
            {"message": {"role": "assistant", "text": "ok"}}]}})
    monkeypatch.setattr(yandex.httpx, "post", fake_post)
    yandex.gpt_complete([{"role": "user", "text": "hi"}],
                        api_key="K", folder_id="F", model="yandexgpt-lite")
    assert captured["json"]["modelUri"] == "gpt://F/yandexgpt-lite/latest"
