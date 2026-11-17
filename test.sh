#!/bin/bash
set -e

cd "$(dirname "$0")"

./build.sh

ctest --test-dir build --parallel "$(nproc)" --output-on-failure "$@"
