import httpx

_TIMEOUT = 20.0
_STT_URL = "https://stt.api.cloud.yandex.net/speech/v1/stt:recognize"


def stt_recognize(audio_lpcm: bytes, *, api_key: str, folder_id: str,
                  sample_rate: int = 16000, lang: str = "ru-RU") -> str:
    resp = httpx.post(
        _STT_URL,
        params={"folderId": folder_id, "lang": lang,
                "format": "lpcm", "sampleRateHertz": sample_rate},
        headers={"Authorization": f"Api-Key {api_key}"},
        content=audio_lpcm,
        timeout=_TIMEOUT,
    )
    resp.raise_for_status()
    return resp.json().get("result", "")
