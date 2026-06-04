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
        {"role": "user", "text": "как дела", "audio": "in_0.wav"},
        {"role": "bot", "text": "нормально", "audio": "0.mp3"}]
    assert url == "http://h:8080/audio/sid-1/0.mp3"
    saved = (tmp_path / "audio" / "sid-1" / "0.mp3").read_bytes()
    assert saved == b"MP3"
    # входящая запись сохранена как WAV (RIFF-заголовок)
    wav = (tmp_path / "audio" / "sid-1" / "in_0.wav").read_bytes()
    assert wav[:4] == b"RIFF" and wav[8:12] == b"WAVE"


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


def test_process_turn_strips_trailing_slash_in_base_url(tmp_path, monkeypatch):
    s = Settings(api_key="K", folder_id="F", public_base_url="http://h:8080/",
                 data_dir=tmp_path, system_prompt="s", max_tokens=10)
    monkeypatch.setattr(pipeline.yandex, "stt_recognize", lambda a, **k: "хай")
    monkeypatch.setattr(pipeline.yandex, "gpt_complete", lambda m, **k: "ок")
    monkeypatch.setattr(pipeline.yandex, "tts_synthesize", lambda t, **k: b"O")
    monkeypatch.setattr(pipeline.audio, "opus_to_mp3", lambda b: b"M")
    url = pipeline.process_turn(s, "sid-9", b"x")
    assert url == "http://h:8080/audio/sid-9/0.mp3"   # ровно один слэш


def test_process_turn_empty_stt_saves_audio_no_reply(tmp_path, monkeypatch):
    s = Settings(data_dir=tmp_path)
    monkeypatch.setattr(pipeline.yandex, "stt_recognize", lambda a, **k: "   ")
    url = pipeline.process_turn(s, "sid-3", b"x")
    assert url is None
    # ход пользователя записан с пустым текстом, запись слышна в истории
    assert sessions.read_history(tmp_path, "sid-3") == [
        {"role": "user", "text": "", "audio": "in_0.wav"}]
    assert (tmp_path / "audio" / "sid-3" / "in_0.wav").exists()
