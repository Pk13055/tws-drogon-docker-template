FROM drogonframework/drogon:latest
LABEL author=pk13055, version=0.3

# Official macOS/Unix API zips unpack to IBJts/{CMakeLists.txt, source/...} plus META-INF/.
# Bump the filename when you replace the SDK zip in this directory.
ARG TWS_SDK_ZIP=twsapi_macunix.1045.01.zip
ARG TWS_SDK_URL=https://interactivebrokers.github.io/downloads/twsapi_macunix.1045.01.zip

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        git \
        libssl-dev \
        unzip \
        libprotobuf-dev \
        protobuf-compiler \
 && git clone --depth 1 --branch v4.3.27 https://github.com/CopernicaMarketingSoftware/AMQP-CPP.git /tmp/amqp-cpp && \
    cmake -S /tmp/amqp-cpp -B /tmp/amqp-cpp-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DAMQP-CPP_LINUX_TCP=ON \
        -DAMQP-CPP_BUILD_SHARED=OFF && \
    cmake --build /tmp/amqp-cpp-build --config Release -j"$(nproc)" && \
    cmake --install /tmp/amqp-cpp-build --prefix /usr/local && \
    rm -rf /tmp/amqp-cpp /tmp/amqp-cpp-build /var/lib/apt/lists/*

# CMake templates missing from IB zip; client CMakeLists needs protobuf + protobufUnix (see cmake/tws-sdk/).
COPY cmake/tws-sdk/twsapiConfig.cmake.in \
     cmake/tws-sdk/twsapiConfigVersion.cmake.in \
     cmake/tws-sdk/client-CMakeLists.txt \
     /tmp/tws-sdk-overlay/

RUN set -eux; \
    curl -fL --retry 5 --retry-delay 2 --connect-timeout 20 --max-time 300 \
      "${TWS_SDK_URL}" -o "/tmp/${TWS_SDK_ZIP}"; \
    unzip -q "/tmp/${TWS_SDK_ZIP}" -d /tmp/tws-sdk-extract; \
    cp /tmp/tws-sdk-overlay/twsapiConfig.cmake.in /tmp/tws-sdk-extract/IBJts/; \
    cp /tmp/tws-sdk-overlay/twsapiConfigVersion.cmake.in /tmp/tws-sdk-extract/IBJts/; \
    cp /tmp/tws-sdk-overlay/client-CMakeLists.txt /tmp/tws-sdk-extract/IBJts/source/cppclient/client/CMakeLists.txt; \
    cd /tmp/tws-sdk-extract/IBJts/source; \
    protoc --proto_path=./proto --experimental_allow_proto3_optional --cpp_out=./cppclient/client/protobufUnix proto/*.proto; \
    cmake -S /tmp/tws-sdk-extract/IBJts -B /tmp/twsapi-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DINSTALL_CMAKE_DIR=/usr/local/lib/cmake/twsapi \
        -DINSTALL_LIB_DIR=/usr/local/lib \
        -DINSTALL_INCLUDE_DIR=/usr/local/include \
        -DIBKR_BUILD_TESTCPPCLIENT=OFF \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5; \
    cmake --build /tmp/twsapi-build --config Release -j"$(nproc)"; \
    cmake --install /tmp/twsapi-build; \
    rm -rf /tmp/tws-sdk-extract /tmp/twsapi-build /tmp/tws-sdk-overlay "/tmp/${TWS_SDK_ZIP}"

WORKDIR /app

COPY . /app

ENTRYPOINT ["./entrypoint.sh"]
