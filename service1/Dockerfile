FROM debian:bookworm-20250908-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake g++ make qt6-base-dev qt6-httpserver-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY service1/ service1/
RUN cmake -S service1 -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel 2

FROM debian:bookworm-20250908-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
    libqt6httpserver6 libqt6sql6-psql libqt6concurrent6 curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home app
COPY --from=build /src/build/service1 /usr/local/bin/service1
COPY LICENSE THIRD_PARTY.md /usr/share/licenses/msa2026-docker/
USER 10001:10001
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/service1"]
