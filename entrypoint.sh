#!/bin/sh
set -eu

cmake -S /app -B /tmp/tws-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/tws-build --config Release

exec /tmp/tws-build/tws
