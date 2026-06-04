from server import sessions

def test_append_and_read_history(tmp_path):
    sid = "abc-123"
    sessions.append_turn(tmp_path, sid, "user", "привет")
    sessions.append_turn(tmp_path, sid, "bot", "здравствуй")
    hist = sessions.read_history(tmp_path, sid)
    assert hist == [("user", "привет"), ("bot", "здравствуй")]

def test_read_history_missing_session(tmp_path):
    assert sessions.read_history(tmp_path, "nope") == []

def test_history_survives_newlines_in_text(tmp_path):
    sid = "s1"
    sessions.append_turn(tmp_path, sid, "user", "строка1\nстрока2")
    hist = sessions.read_history(tmp_path, sid)
    assert hist == [("user", "строка1 строка2")]

def test_history_collapses_crlf(tmp_path):
    sid = "s-crlf"
    sessions.append_turn(tmp_path, sid, "bot", "строка1\r\nстрока2")
    hist = sessions.read_history(tmp_path, sid)
    assert hist == [("bot", "строка1 строка2")]

def test_read_history_does_not_create_dirs(tmp_path):
    sessions.read_history(tmp_path, "ghost")
    assert not (tmp_path / "sessions").exists()

def test_safe_id_blocks_path_traversal(tmp_path):
    # враждебный session_id не должен вырваться за data_dir
    sessions.append_turn(tmp_path, "../../etc/passwd", "user", "x")
    files = list((tmp_path / "sessions").glob("*.txt"))
    assert len(files) == 1
    assert files[0].name == "etcpasswd.txt"

def test_next_audio_path_increments(tmp_path):
    sid = "s2"
    p1 = sessions.next_audio_path(tmp_path, sid)
    p1.write_bytes(b"x")
    p2 = sessions.next_audio_path(tmp_path, sid)
    assert p1.name == "0.mp3"
    assert p2.name == "1.mp3"
    assert p1.parent == p2.parent
