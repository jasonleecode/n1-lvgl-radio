FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config libsdl2-dev libpulse-dev python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
