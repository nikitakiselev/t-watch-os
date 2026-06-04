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
    path = settings.data_dir / "audio" / session_id / name
    if not path.is_file():
        raise HTTPException(status_code=404, detail="not found")
    return FileResponse(path, media_type="audio/mpeg")
