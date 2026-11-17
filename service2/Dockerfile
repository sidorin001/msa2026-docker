FROM python:3.12.8-slim-bookworm AS build
WORKDIR /build
COPY service2/requirements.txt .
RUN pip wheel --no-cache-dir --only-binary=:all: --wheel-dir /wheels -r requirements.txt \
    && python -m venv /opt/venv \
    && /opt/venv/bin/pip install --no-cache-dir --no-index --find-links=/wheels -r requirements.txt

FROM python:3.12.8-slim-bookworm AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home app
COPY --from=build /opt/venv /opt/venv
WORKDIR /app
COPY service2/app/ app/
COPY LICENSE THIRD_PARTY.md /usr/share/licenses/msa2026-docker/
USER 10001:10001
EXPOSE 8080
ENTRYPOINT ["/opt/venv/bin/python", "-m", "app.main"]
