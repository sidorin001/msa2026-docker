"""Test-only wrapper. Mounted only by compose.readiness-test.yaml; never in runtime images."""
import os
from pathlib import Path
import uvicorn
from fastapi.responses import JSONResponse
from app.main import app


@app.middleware("http")
async def readiness_fault(request, call_next):
    if request.url.path == "/ready" and Path(os.environ["READINESS_FAILURE_FILE"]).exists():
        return JSONResponse({"status": "test-unready"}, status_code=503)
    return await call_next(request)


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8080, access_log=False)
