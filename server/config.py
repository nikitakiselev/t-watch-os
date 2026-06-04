import os
from dataclasses import dataclass, field
from pathlib import Path


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
    system_prompt: str = field(default_factory=lambda: os.getenv(
        "SYSTEM_PROMPT",
        "Ты голосовой ассистент в наручных часах. Отвечай очень кратко, "
        "разговорно, одной-двумя фразами, без списков и markdown."))
    max_tokens: int = field(default_factory=lambda: int(os.getenv("MAX_TOKENS", "120")))


def get_settings() -> Settings:
    return Settings()
