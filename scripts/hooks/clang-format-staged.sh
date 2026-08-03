#!/bin/sh
command -v clang-format >/dev/null 2>&1 || exit 0
clang-format -i "$@"
