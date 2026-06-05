import array
import struct
from server import audio


def _peak(pcm):
    a = array.array("h"); a.frombytes(pcm)
    return max((abs(s) for s in a), default=0)


def test_normalize_lpcm_amplifies_quiet():
    quiet = b"".join(struct.pack("<h", v) for v in [0, 500, -1000, 800, -300])
    assert _peak(audio.normalize_lpcm(quiet)) == 16000     # пик 1000 × cap 16 (cap сработал)

def test_normalize_lpcm_reaches_target_when_uncapped():
    # пик 4000 → усиление ~7.4× (под cap), нормализуется к ~0.9 шкалы
    sig = b"".join(struct.pack("<h", v) for v in [4000, -4000, 2000, -1000])
    assert _peak(audio.normalize_lpcm(sig)) > 28000

def test_normalize_lpcm_silence_unchanged():
    silence = b"\x00\x00" * 100
    assert audio.normalize_lpcm(silence) == silence

def test_normalize_lpcm_caps_gain():
    tiny = struct.pack("<h", 1) * 50                       # пик 1
    assert _peak(audio.normalize_lpcm(tiny, max_gain=16.0)) <= 16

def test_normalize_lpcm_loud_unchanged():
    loud = struct.pack("<h", 32000) * 10                   # уже почти полная шкала
    assert audio.normalize_lpcm(loud) == loud


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
