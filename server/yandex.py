import httpx

# Все три сервиса Yandex Cloud (STT/GPT/TTS) авторизуются одинаково — Api-Key.
_TIMEOUT = 20.0
_STT_URL = "https://stt.api.cloud.yandex.net/speech/v1/stt:recognize"
_GPT_URL = "https://llm.api.cloud.yandex.net/foundationModels/v1/completion"
_TTS_URL = "https://tts.api.cloud.yandex.net/speech/v1/tts:synthesize"


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


def gpt_complete(messages: list, *, api_key: str, folder_id: str,
                 max_tokens: int = 120, temperature: float = 0.6) -> str:
    resp = httpx.post(
        _GPT_URL,
        json={
            "modelUri": f"gpt://{folder_id}/yandexgpt-lite/latest",
            "completionOptions": {"stream": False,
                                  "temperature": temperature,
                                  "maxTokens": str(max_tokens)},
            "messages": messages,
        },
        headers=_auth(api_key),
        timeout=_TIMEOUT,
    )
    resp.raise_for_status()
    alts = resp.json()["result"]["alternatives"]
    return alts[0]["message"]["text"] if alts else ""


def tts_synthesize(text: str, *, api_key: str, folder_id: str,
                   voice: str = "alena", lang: str = "ru-RU") -> bytes:
    resp = httpx.post(
        _TTS_URL,
        data={"text": text, "lang": lang, "voice": voice,
              "format": "oggopus", "folderId": folder_id},
        headers=_auth(api_key),
        timeout=_TIMEOUT,
    )
    resp.raise_for_status()
    return resp.content
