import os, time
from server import cleanup

def test_cleanup_removes_old_and_keeps_fresh(tmp_path):
    sess = tmp_path / "sessions"; sess.mkdir()
    aud = tmp_path / "audio" / "old"; aud.mkdir(parents=True)
    old_txt = sess / "old.txt"; old_txt.write_text("x")
    old_mp3 = aud / "0.mp3"; old_mp3.write_bytes(b"x")
    fresh = sess / "fresh.txt"; fresh.write_text("y")

    old_time = time.time() - 10_000
    os.utime(old_txt, (old_time, old_time))
    os.utime(old_mp3, (old_time, old_time))
    os.utime(aud, (old_time, old_time))

    cleanup.cleanup_old(tmp_path, ttl_seconds=3600, now=time.time())

    assert not old_txt.exists()
    assert not aud.exists()        # пустой каталог сессии тоже убран
    assert fresh.exists()

def test_cleanup_cold_start_no_dirs(tmp_path):
    # первый запуск сервера: каталогов sessions/audio ещё нет — не должно падать
    cleanup.cleanup_old(tmp_path, ttl_seconds=3600, now=time.time())
