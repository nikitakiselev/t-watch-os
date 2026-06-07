from server import audio, sessions, yandex
from server.config import Settings

_ROLE_MAP = {"user": "user", "bot": "assistant"}


def _build_messages(settings: Settings, history: list) -> list:
    msgs = [{"role": "system", "text": settings.system_prompt}]
    for turn in history:
        if not turn["text"]:                       # пропускаем нераспознанные ходы (пустой текст)
            continue
        msgs.append({"role": _ROLE_MAP.get(turn["role"], "user"), "text": turn["text"]})
    return msgs


def transcribe(settings: Settings, audio_lpcm: bytes) -> str:
    # Чистый STT для голосовых заметок: ни GPT, ни TTS, ни истории сессий —
    # просто распознаём речь в текст и отдаём его клиенту.
    return yandex.stt_recognize(
        audio_lpcm, api_key=settings.api_key, folder_id=settings.folder_id).strip()


def process_turn(settings: Settings, session_id: str, audio_lpcm: bytes):
    # 1. Сохраняем входящую запись как WAV (нормализуем громкость — PDM-мик тихий).
    user_wav = sessions.next_user_audio_path(settings.data_dir, session_id)
    user_wav.write_bytes(audio.lpcm_to_wav(audio.normalize_lpcm(audio_lpcm)))

    # 2. STT. Ход пользователя пишем всегда (даже если не распознано — запись слышна в истории).
    user_text = yandex.stt_recognize(
        audio_lpcm, api_key=settings.api_key, folder_id=settings.folder_id).strip()
    sessions.append_turn(settings.data_dir, session_id, "user", user_text, audio=user_wav.name)
    if not user_text:
        return None

    history = sessions.read_history(settings.data_dir, session_id)
    messages = _build_messages(settings, history)

    reply = yandex.gpt_complete(
        messages, api_key=settings.api_key, folder_id=settings.folder_id,
        model=settings.gpt_model, max_tokens=settings.max_tokens).strip()

    # 3. TTS-ответ → mp3, привязываем к ходу бота.
    opus = yandex.tts_synthesize(
        reply, api_key=settings.api_key, folder_id=settings.folder_id,
        voice=settings.voice)
    path = sessions.next_audio_path(settings.data_dir, session_id)
    path.write_bytes(audio.opus_to_mp3(opus))
    sessions.append_turn(settings.data_dir, session_id, "bot", reply, audio=path.name)

    # rstrip('/') — PUBLIC_BASE_URL может быть со слэшем на конце (иначе вышло бы '//audio').
    # path.parent.name (== _safe_id) — чтобы GET /audio нашёл файл даже при «грязном» session_id.
    base = settings.public_base_url.rstrip("/")
    return f"{base}/audio/{path.parent.name}/{path.name}"
