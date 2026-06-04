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

    assert seen["messages"][0] == {"role": "system", "text": "будь краток"}
    assert seen["messages"][-1] == {"role": "user", "text": "как дела"}
    assert sessions.read_history(tmp_path, "sid-1") == [
        ("user", "как дела"), ("bot", "нормально")]
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
                        lambda m, **k: seen.setdefault("m", m) and "Кит")
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
