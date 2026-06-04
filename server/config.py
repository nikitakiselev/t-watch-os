import os
from dataclasses import dataclass, field
from pathlib import Path

# Конфиг намеренно на stdlib-dataclass, а не pydantic-settings: держим зависимости
# и docker-образ минимальными. get_settings() читает env при каждом вызове, поэтому
# default_factory срабатывает на момент создания Settings (важно для тестов с monkeypatch).


@dataclass
class Settings:
    api_key: str = field(default_factory=lambda: os.getenv("YANDEX_API_KEY", ""))
    folder_id: str = field(default_factory=lambda: os.getenv("YANDEX_FOLDER_ID", ""))
    public_base_url: str = field(
        default_factory=lambda: os.getenv("PUBLIC_BASE_URL", "http://localhost:8080"))
    session_ttl: int = field(
        default_factory=lambda: int(os.getenv("SESSION_TTL", "86400")))
    data_dir: Path = field(
        default_factory=lambda: Path(os.getenv("DATA_DIR", "data")))
    voice: str = field(default_factory=lambda: os.getenv("YANDEX_VOICE", "alena"))
    gpt_model: str = field(default_factory=lambda: os.getenv("YANDEX_GPT_MODEL", "yandexgpt"))
    system_prompt: str = field(default_factory=lambda: os.getenv(
        "SYSTEM_PROMPT",
        "Ты дружелюбный голосовой ассистент. Отвечай естественно, разговорно и по "
        "существу, без markdown и списков (ответ озвучивается голосом). Обычные "
        "вопросы — держи коротко, 1-3 предложения. Но если просят рассказать "
        "стихотворение, историю, шутку или дать развёрнутый ответ — выполняй "
        "полноценно, не обрывай и не отказывайся."))
    max_tokens: int = field(default_factory=lambda: int(os.getenv("MAX_TOKENS", "600")))


def get_settings() -> Settings:
    return Settings()
