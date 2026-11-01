FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config libsdl2-dev libpulse-dev libjson-c-dev \
    libliquid-dev libsndfile1-dev nlohmann-json3-dev meson ninja-build git python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
