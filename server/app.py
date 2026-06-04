import re
from fastapi import FastAPI, Request, Response, HTTPException
from fastapi.responses import JSONResponse, FileResponse

from server import pipeline
from server.config import get_settings

app = FastAPI(title="watchos AI assistant")

_SAFE = re.compile(r"^[A-Za-z0-9_-]+$")


@app.post("/talk")
async def talk(session: str, request: Request):
    settings = get_settings()
    audio_lpcm = await request.body()
    url = pipeline.process_turn(settings, session, audio_lpcm)
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
