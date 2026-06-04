from server import audio, sessions, yandex
from server.config import Settings

_ROLE_MAP = {"user": "user", "bot": "assistant"}


def _build_messages(settings: Settings, history: list) -> list:
    msgs = [{"role": "system", "text": settings.system_prompt}]
    for role, text in history:
        msgs.append({"role": _ROLE_MAP.get(role, "user"), "text": text})
    return msgs


def process_turn(settings: Settings, session_id: str, audio_lpcm: bytes):
    user_text = yandex.stt_recognize(
        audio_lpcm, api_key=settings.api_key, folder_id=settings.folder_id).strip()
    if not user_text:
        return None

    sessions.append_turn(settings.data_dir, session_id, "user", user_text)
    history = sessions.read_history(settings.data_dir, session_id)
    messages = _build_messages(settings, history)

    reply = yandex.gpt_complete(
        messages, api_key=settings.api_key, folder_id=settings.folder_id,
        max_tokens=settings.max_tokens).strip()
    sessions.append_turn(settings.data_dir, session_id, "bot", reply)

    opus = yandex.tts_synthesize(
        reply, api_key=settings.api_key, folder_id=settings.folder_id,
        voice=settings.voice)
    mp3 = audio.opus_to_mp3(opus)

    path = sessions.next_audio_path(settings.data_dir, session_id)
    path.write_bytes(mp3)
    # rstrip('/') — PUBLIC_BASE_URL может быть со слэшем на конце (иначе вышло бы '//audio').
    # path.parent.name (== _safe_id) — чтобы GET /audio нашёл файл даже при «грязном» session_id.
    base = settings.public_base_url.rstrip("/")
    return f"{base}/audio/{path.parent.name}/{path.name}"
