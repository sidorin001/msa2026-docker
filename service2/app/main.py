"""Read-only Items API. Synchronous database work uses Starlette's worker pool."""
import os
import re
import logging
import psycopg
from psycopg.rows import dict_row
import uvicorn
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None, redirect_slashes=False)
INSTANCE = os.environ.get("INSTANCE_ID", "service2-v1")
log = logging.getLogger("uvicorn.error")


def positive(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]+", value) or len(value) > 10:
        raise ValueError("id")
    result = int(value)
    if not 0 < result <= 2147483647:
        raise ValueError("id")
    return result


def error(status, code, message):
    return JSONResponse({"error": {"code": code, "message": message}}, status_code=status)


def query(statement, params=()):
    with psycopg.connect(
        host=os.environ["DB_HOST"], port=int(os.getenv("DB_PORT", "5432")),
        dbname=os.environ["DB_NAME"], user=os.environ["DB_USER"], password=os.environ["DB_PASSWORD"],
        connect_timeout=positive(os.getenv("DB_CONNECT_TIMEOUT_SECONDS", "2")),
        options=f"-c statement_timeout={positive(os.getenv('DB_STATEMENT_TIMEOUT_MS', '3000'))}",
        autocommit=True, row_factory=dict_row,
    ) as connection:
        return connection.execute(statement, params).fetchall()


@app.middleware("http")
async def identify(request: Request, call_next):
    response = await call_next(request)
    response.headers["X-Service2-Instance"] = INSTANCE
    log.info("instance=%s method=%s status=%s", INSTANCE, request.method, response.status_code)
    return response


@app.exception_handler(Exception)
async def unexpected(_request, _exception):
    response = error(500, "internal_error", "Internal server error")
    response.headers["X-Service2-Instance"] = INSTANCE
    return response


@app.api_route("/{path:path}", methods=["GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", "TRACE", "CONNECT"])
def dispatch(path: str, request: Request):
    match = re.fullmatch(r"api/v1/items/([^/]+)", path)
    if path not in ("health", "ready", "api/v1/items") and not match:
        return error(404, "not_found", "Route not found")
    if request.method not in ("GET", "HEAD"):
        response = error(405, "method_not_allowed", "Only GET and HEAD are supported")
        response.headers["Allow"] = "GET, HEAD"
        return response
    try:
        if path == "health":
            return {"status": "ok"}
        if path == "ready":
            query("SELECT 1")
            return {"status": "ready"}
        if match:
            identifier = positive(match[1])
            rows = query("SELECT id, group_id, name FROM items WHERE id = %s", (identifier,))
            return rows[0] if rows else error(404, "not_found", "Item not found")
        groups = request.query_params.getlist("group_id")
        if groups:
            if len(groups) != 1:
                raise ValueError("duplicate group_id")
            identifier = positive(groups[0])
            return query("SELECT id, group_id, name FROM items WHERE group_id = %s ORDER BY id", (identifier,))
        return query("SELECT id, group_id, name FROM items ORDER BY id")
    except ValueError:
        return error(400, "invalid_id", "Expected a positive 32-bit integer")
    except psycopg.Error:
        return error(503, "database_unavailable", "Database unavailable")


if __name__ == "__main__":
    for variable in ("DB_HOST", "DB_NAME", "DB_USER", "DB_PASSWORD"):
        if not os.environ.get(variable):
            raise SystemExit(f"Missing configuration: {variable}")
    port = positive(os.getenv("HTTP_PORT", "8080"))
    if port > 65535:
        raise SystemExit("Invalid HTTP_PORT")
    uvicorn.run(app, host="0.0.0.0", port=port, access_log=False, server_header=False)
