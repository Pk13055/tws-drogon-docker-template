FROM drogonframework/drogon:latest AS base-deps

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        git \
        libssl-dev \
        unzip \
        libprotobuf-dev \
        protobuf-compiler \
        libintelrdfpmath-dev \
        ninja-build \
        ccache \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Compiler cache
ENV CC="ccache gcc"
ENV CXX="ccache g++"
ENV CCACHE_DIR=/root/.ccache
ENV CCACHE_MAXSIZE=2G

# ---------- Stage 2: AMQP-CPP ----------
FROM base-deps AS amqp

RUN git clone --depth 1 --branch v4.3.27 https://github.com/CopernicaMarketingSoftware/AMQP-CPP.git /tmp/amqp-cpp && \
    cmake -S /tmp/amqp-cpp -B /tmp/amqp-cpp-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DAMQP-CPP_LINUX_TCP=ON \
        -DAMQP-CPP_BUILD_SHARED=OFF && \
    cmake --build /tmp/amqp-cpp-build -j"$(nproc)" && \
    cmake --install /tmp/amqp-cpp-build --prefix /usr/local && \
    rm -rf /tmp/amqp-cpp /tmp/amqp-cpp-build

# ---------- Stage 3: TWS SDK ----------
FROM amqp AS tws-sdk

ARG TWS_SDK_ZIP=twsapi_macunix.1045.01.zip
ARG TWS_SDK_URL=https://interactivebrokers.github.io/downloads/twsapi_macunix.1045.01.zip

# Only SDK overlay (rarely changes)
COPY cmake/tws-sdk/ /tmp/tws-sdk-overlay/

RUN set -eux; \
    curl -fL --retry 5 --retry-delay 2 \
      "${TWS_SDK_URL}" -o "/tmp/${TWS_SDK_ZIP}"; \
    unzip -q "/tmp/${TWS_SDK_ZIP}" -d /tmp/tws-sdk-extract; \
    cp /tmp/tws-sdk-overlay/twsapiConfig.cmake.in /tmp/tws-sdk-extract/IBJts/; \
    cp /tmp/tws-sdk-overlay/twsapiConfigVersion.cmake.in /tmp/tws-sdk-extract/IBJts/; \
    cp /tmp/tws-sdk-overlay/client-CMakeLists.txt \
       /tmp/tws-sdk-extract/IBJts/source/cppclient/client/CMakeLists.txt; \
    cd /tmp/tws-sdk-extract/IBJts/source; \
    protoc --proto_path=./proto \
        --experimental_allow_proto3_optional \
        --cpp_out=./cppclient/client/protobufUnix proto/*.proto; \
    cmake -S /tmp/tws-sdk-extract/IBJts -B /tmp/twsapi-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DINSTALL_CMAKE_DIR=/usr/local/lib/cmake/twsapi \
        -DINSTALL_LIB_DIR=/usr/local/lib \
        -DINSTALL_INCLUDE_DIR=/usr/local/include \
        -DIBKR_BUILD_TESTCPPCLIENT=OFF \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5; \
    cmake --build /tmp/twsapi-build -j"$(nproc)"; \
    cmake --install /tmp/twsapi-build; \
    rm -rf /tmp/tws-sdk-extract /tmp/twsapi-build /tmp/tws-sdk-overlay "/tmp/${TWS_SDK_ZIP}"

# ---------- Stage 4: Builder ----------
FROM tws-sdk AS builder

WORKDIR /app

# ---- Stable inputs first ----
COPY CMakeLists.txt .
COPY cmake/ cmake/
COPY include/ include/

# ---- Source MUST be here for CMake ----
COPY src/ src/

# Configure
RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -G Ninja

# Build
RUN --mount=type=cache,target=/root/.ccache \
    cmake --build build -j"$(nproc)"

# ---------- Stage 5: Runtime ----------
FROM tws-sdk AS runtime

WORKDIR /app

COPY --from=builder /app/build/tws /app/tws
COPY entrypoint.sh .
ENTRYPOINT ["./entrypoint.sh"]
