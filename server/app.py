import asyncio, logging, time
import re
from pathlib import Path
from fastapi import FastAPI, Request, Response, HTTPException
from fastapi.responses import JSONResponse, FileResponse
from starlette.concurrency import run_in_threadpool

from server import cleanup, pipeline
from server.config import get_settings

app = FastAPI(title="watchos AI assistant")

_SAFE = re.compile(r"^[A-Za-z0-9_-]+$")
_INDEX = Path(__file__).resolve().parent / "static" / "index.html"


@app.get("/")
def index():
    return FileResponse(_INDEX, media_type="text/html")


@app.post("/talk")
async def talk(session: str, request: Request):
    settings = get_settings()
    audio_lpcm = await request.body()
    # process_turn блокирующий (STT+GPT+TTS, ~3-10 c) — уводим в пул потоков,
    # чтобы не вешать event loop (фоновую чистку, параллельные запросы).
    url = await run_in_threadpool(pipeline.process_turn, settings, session, audio_lpcm)
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
