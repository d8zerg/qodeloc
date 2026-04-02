FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      cmake \
      curl \
      git \
      ninja-build \
      pkg-config \
      python3 \
      python3-pip && \
    rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir conan

WORKDIR /src

COPY VERSION /src/VERSION
COPY core /src/core
COPY prompts /src/prompts

RUN cd /src/core && \
    conan profile detect --force && \
    conan install . -of build/debug -s build_type=Debug -s compiler.cppstd=23 \
      -g CMakeDeps -g CMakeToolchain --build=missing && \
    cmake --preset debug && \
    cmake --build --preset debug --parallel "$(nproc)"

WORKDIR /workspace/qodeloc

EXPOSE 3100

ENTRYPOINT ["/src/core/build/debug/qodeloc-core"]
