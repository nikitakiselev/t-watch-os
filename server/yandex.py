import httpx

# Все три сервиса Yandex Cloud (STT/GPT/TTS) авторизуются одинаково — Api-Key.
_TIMEOUT = 20.0
_STT_URL = "https://stt.api.cloud.yandex.net/speech/v1/stt:recognize"


def _auth(api_key: str) -> dict:
    return {"Authorization": f"Api-Key {api_key}"}


def stt_recognize(audio_lpcm: bytes, *, api_key: str, folder_id: str,
                  sample_rate: int = 16000, lang: str = "ru-RU") -> str:
    resp = httpx.post(
        _STT_URL,
        params={"folderId": folder_id, "lang": lang,
                "format": "lpcm", "sampleRateHertz": sample_rate},
        headers=_auth(api_key),
        content=audio_lpcm,
        timeout=_TIMEOUT,
    )
    resp.raise_for_status()
    return resp.json().get("result", "")
