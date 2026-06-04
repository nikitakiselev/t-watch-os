import subprocess


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
