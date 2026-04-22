#!/bin/bash
set -e

cd "$(dirname "$0")"

cmake -G Ninja -B build . -DCMAKE_BUILD_TYPE=Debug
ninja -C build