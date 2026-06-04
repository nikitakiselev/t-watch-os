import asyncio, base64, logging, secrets, time
import re
from pathlib import Path
from fastapi import FastAPI, Request, Response, HTTPException
from fastapi.responses import JSONResponse, FileResponse, HTMLResponse
from starlette.concurrency import run_in_threadpool

from server import cleanup, pipeline
from server.config import get_settings

app = FastAPI(title="watchos AI assistant")

_SAFE = re.compile(r"^[A-Za-z0-9_-]+$")
_INDEX = Path(__file__).resolve().parent / "static" / "index.html"


def _basic_auth_ok(request: Request, password: str) -> bool:
    # Любое имя пользователя, пароль == WEB_PASSWORD. Пустой пароль → проверка выключена.
    if not password:
        return True
    header = request.headers.get("authorization", "")
    if not header.startswith("Basic "):
        return False
    try:
        _, _, given = base64.b64decode(header[6:]).decode("utf-8").partition(":")
    except Exception:
        return False
    return secrets.compare_digest(given, password)


@app.get("/")
def index(request: Request):
    settings = get_settings()
    if not _basic_auth_ok(request, settings.web_password):
        return Response(status_code=401,
                        headers={"WWW-Authenticate": 'Basic realm="assistant"'})
    # API-ключ встраиваем в страницу, чтобы её JS мог авторизоваться на /talk.
    html = _INDEX.read_text(encoding="utf-8").replace("__API_KEY__", settings.server_api_key)
    return HTMLResponse(html)


@app.post("/talk")
async def talk(session: str, request: Request):
    settings = get_settings()
    if settings.server_api_key:
        given = request.headers.get("x-api-key", "")
        if not secrets.compare_digest(given, settings.server_api_key):
            return JSONResponse({"error": "unauthorized"}, status_code=401)
    audio_lpcm = await request.body()
    # process_turn блокирующий (STT+GPT+TTS, ~3-10 c) — уводим в пул потоков,
    # чтобы не вешать event loop (фоновую чистку, параллельные запросы).
    try:
        url = await run_in_threadpool(pipeline.process_turn, settings, session, audio_lpcm)
    except Exception as e:
        # Сбой Yandex/ffmpeg (плохой ключ, сеть, квота) — отдаём причину, а не сырой 500.
        logging.getLogger(__name__).exception("talk failed")
        return JSONResponse({"error": f"{type(e).__name__}: {e}"}, status_code=502)
    if url is None:
        return Response(status_code=204)
    return JSONResponse({"url": url})


@app.get("/audio/{session_id}/{name}")
def audio(session_id: str, name: str):
    settings = get_settings()
    if not _SAFE.match(session_id) or not _SAFE.match(name.replace(".", "")):
        raise HTTPException(status_code=400, detail="bad path")
    base = (settings.data_dir / "audio").resolve()
    path = (base / session_id / name).resolve()
    if not path.is_relative_to(base):          # defence-in-depth: не выпускаем за data_dir/audio
        raise HTTPException(status_code=400, detail="bad path")
    if not path.is_file():
        raise HTTPException(status_code=404, detail="not found")
    return FileResponse(path, media_type="audio/mpeg")


@app.on_event("startup")
async def _start_cleanup():
    async def loop():
        while True:
            try:
                s = get_settings()
                cleanup.cleanup_old(s.data_dir, s.session_ttl, time.time())
            except Exception:                       # цикл должен пережить разовый сбой FS
                logging.getLogger(__name__).exception("session cleanup failed")
            await asyncio.sleep(3600)
    asyncio.create_task(loop())
