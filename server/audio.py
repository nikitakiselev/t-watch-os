import struct
import subprocess


def lpcm_to_wav(pcm: bytes, sample_rate: int = 16000, channels: int = 1,
                bits: int = 16) -> bytes:
    # Обернуть сырой LPCM в 44-байтный WAV-заголовок (lossless, играется в браузере).
    byte_rate = sample_rate * channels * bits // 8
    block_align = channels * bits // 8
    data_len = len(pcm)
    return (b"RIFF" + struct.pack("<I", 36 + data_len) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, sample_rate,
                                    byte_rate, block_align, bits)
            + b"data" + struct.pack("<I", data_len) + pcm)


def opus_to_mp3(opus: bytes) -> bytes:
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error",
           "-i", "pipe:0", "-f", "mp3", "-codec:a", "libmp3lame",
           "-q:a", "5", "pipe:1"]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate(input=opus)
    if proc.returncode != 0:
        raise RuntimeError(f"ffmpeg failed: {err.decode('utf-8', 'replace')}")
    return out
