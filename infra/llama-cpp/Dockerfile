# syntax=docker/dockerfile:1.7

ARG UBUNTU_VERSION=24.04

FROM ubuntu:${UBUNTU_VERSION} AS build
ARG DEBIAN_FRONTEND=noninteractive
ARG LLAMA_CPP_REF=master

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    git \
    libcurl4-openssl-dev \
    libopenblas-dev \
    libssl-dev \
    ninja-build \
    wget \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN git clone --depth 1 --branch "${LLAMA_CPP_REF}" https://github.com/ggml-org/llama.cpp.git

WORKDIR /src/llama.cpp
RUN cmake -B build -G Ninja \
    -DGGML_NATIVE=ON \
    -DGGML_BLAS=ON \
    -DGGML_BLAS_VENDOR=OpenBLAS \
    -DLLAMA_BUILD_SERVER=ON \
    -DLLAMA_BUILD_TOOLS=ON \
 && cmake --build build --target llama-server --parallel "$(nproc)"

FROM ubuntu:${UBUNTU_VERSION} AS runtime
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    libcurl4 \
    libgomp1 \
    libopenblas0-pthread \
    libssl3 \
    wget \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/llama.cpp/build/bin/llama-server /usr/local/bin/llama-server

EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/llama-server"]
