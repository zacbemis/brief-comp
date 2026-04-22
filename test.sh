#!/bin/bash
set -e

cd "$(dirname "$0")"

./build.sh

ctest --test-dir build --output-on-failure "$@"
