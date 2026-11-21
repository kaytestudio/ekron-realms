# -----------------------------------------------------------------------------
# Ekron Realms - reproducible Linux build (Ubuntu 24.04, x86_64 or arm64 host).
#
# Builds the whole desktop project with CMake, like the Debian CI job:
#   ekron-realms        C++17 SDL2/OpenGL sandbox
#   librealms_core.so   Free Pascal Realms core (Quake 2 stack)
#   realms-pascal       Free Pascal client: the Pascal core compiled in, running
#                       the C++ sandbox from libekron_sandbox.so
#   realms-launcher     Free Pascal launcher
# runs the CTest suite and packages a .deb. Everything lands in /app/dist:
#
#   docker build -t ekron-builder .
#   docker run --rm -v "$PWD/output:/output" ekron-builder cp -r /app/dist/. /output/
#   (or: docker compose up)
# -----------------------------------------------------------------------------
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config \
        libsdl2-dev libgl1-mesa-dev libssl-dev \
        fpc fp-units-fcl \
        file dpkg-dev ca-certificates && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DENABLE_FPC_CLIENT=ON \
        -DENABLE_PASCAL_CORE=ON && \
    cmake --build build -j"$(nproc)"

RUN ctest --test-dir build --output-on-failure

RUN cd build && cpack -G DEB && \
    mkdir -p /app/dist && \
    cp ekron-realms_*.deb ekron-realms realms-pascal realms-launcher \
       libekron_sandbox.so librealms_core.so /app/dist/ && \
    ls -l /app/dist

CMD ["bash"]
