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
