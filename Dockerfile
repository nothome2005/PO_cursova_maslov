# ==========================================
# Stage 1: Build C++20 Search Engine & CLI
# ==========================================
FROM ubuntu:24.04 AS cpp-builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    libasio-dev \
    libpthread-stubs0-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/build

COPY CMakeLists.txt /app/
COPY server/ /app/server/
COPY cli/ /app/cli/

RUN cmake -B /app/build -S /app -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && ninja -j$(nproc)

# ==========================================
# Stage 2: Minimal Runtime Image
# ==========================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV DATA_DIR=/app/data/documents
ENV OLLAMA_HOST=ollama
ENV OLLAMA_PORT=11434
ENV OLLAMA_MODEL=llama3.2:1b
ENV PORT=8088

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    unzip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Копіюємо скомпільовані бінарні файли C++
COPY --from=cpp-builder /app/build/server/search_server /app/search_server
COPY --from=cpp-builder /app/build/cli/search /app/search

# Копіюємо автономний Web UI
COPY public/ /app/public/

# Створюємо директорії для текстового датасету
RUN mkdir -p /app/data/documents

EXPOSE 8088

CMD ["/app/search_server"]
