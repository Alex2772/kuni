# syntax=docker/dockerfile:1

# ---------------------------------------------------------------------------
# Builder stage: compiles the Kuni core (headless server binary) using Clang.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG CLANG_VERSION=22

# Base build tooling + AUI dependencies (see .github/workflows/build.yml)
RUN apt-get update && apt-get install -y --no-install-recommends \
        wget \
        gnupg \
        software-properties-common \
        ca-certificates \
        git \
        cmake \
        ninja-build \
        pkg-config \
        libglew-dev \
        zlib1g-dev \
        libssl-dev \
        libcrypt-dev \
        libcurl4-openssl-dev \
        libgtk-4-dev \
        libadwaita-1-dev \
        libdbus-1-dev \
        libfontconfig-dev \
        libpulse-dev \
        gperf \
    && rm -rf /var/lib/apt/lists/*

# Install Clang/LLVM (matches CI toolchain requirement: Clang 20+)
RUN wget https://apt.llvm.org/llvm.sh -O /tmp/llvm.sh \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh ${CLANG_VERSION} all \
    && rm /tmp/llvm.sh

ENV CC=clang-${CLANG_VERSION}
ENV CXX=clang++-${CLANG_VERSION}

WORKDIR /src

# Copy only what CMake actually needs to configure & build the project.
# (avoids dragging in build/, cmake-build-*/, .git/, data/, etc.)
COPY CMakeLists.txt aui.boot.cmake ./
COPY src/ ./src/
COPY tests/ ./tests/
COPY assets/ ./assets/

# Configure & build the kuni target in RelWithDebInfo (mirrors CI).
# AUI.Boot caches downloaded/built dependencies in ~/.aui (/root/.aui here) - persist
# it across builds with a BuildKit cache mount so re-runs don't re-fetch/rebuild them.
RUN --mount=type=cache,target=/root/.aui,sharing=locked \
    cmake -G "Ninja" -B /src/build \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DAUI_INSTALL_RUNTIME_DEPENDENCIES=TRUE \
    && cmake --build /src/build --config RelWithDebInfo --target kuni

# ---------------------------------------------------------------------------
# Runtime stage: minimal image containing only the built kuni binary and
# its runtime shared library dependencies.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libglew2.2 \
        zlib1g \
        libssl3 \
        libcurl4 \
        libgtk-4-1 \
        libadwaita-1-0 \
        libdbus-1-3 \
        libfontconfig1 \
        libpulse0 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --shell /usr/sbin/nologin kuni
WORKDIR /home/kuni/app

COPY --from=builder /src/build/bin/ ./bin/

# Kuni writes/reads these paths relative to its CWD (working directory below).
# They must persist across container restarts and are worth mounting/backing up:
#   cache/       - image embeddings & other caches (src/tools/search_photo_in_gallery.cpp)
#   data/        - diary (RAG memory), gallery, voice messages, working_memory.md, comfy prompts
#   logs/        - application logs
#   logs_proxy/  - proxy server request logs (src/proxy_server/proxy_server.cpp)
#   prompts/     - user-editable LLM prompts, generated on first run (src/prompts.cpp)
#   tdlib/       - TDLib session database (auth state, chat cache) (TelegramClientImpl.cpp)
#   config.toml  - main configuration file (src/config.cpp)
RUN mkdir -p cache data logs logs_proxy prompts tdlib \
    && chown -R kuni:kuni /home/kuni/app

VOLUME ["/home/kuni/app/cache", \
        "/home/kuni/app/data", \
        "/home/kuni/app/logs", \
        "/home/kuni/app/logs_proxy", \
        "/home/kuni/app/prompts", \
        "/home/kuni/app/tdlib"]

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libglew2.2 \
        zlib1g \
        libssl3 \
        libcurl4 \
        libgtk-4-1 \
        libadwaita-1-0 \
        libdbus-1-3 \
        libfontconfig1 \
        libpulse0 \
        ca-certificates \
        libopengl0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /home/kuni/app
USER kuni

ENTRYPOINT ["/home/kuni/app/bin/kuni"]
