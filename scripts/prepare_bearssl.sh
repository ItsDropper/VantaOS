#!/bin/sh
set -eu
config="third_party/bearssl/src/config.h"
test -f "$config" || { echo "BearSSL submodule is missing; check it out recursively." >&2; exit 1; }
# BearSSL defaults to POSIX entropy/time APIs. VantaOS injects hardware entropy
# and caller-supplied UTC time, so host OS hooks must be disabled for freestanding.
sed -i \
  -e 's/^#define BR_USE_URANDOM[[:space:]]*1$/#define BR_USE_URANDOM   0/' \
  -e 's/^#define BR_USE_WIN32_RAND[[:space:]]*1$/#define BR_USE_WIN32_RAND   0/' \
  -e 's/^#define BR_USE_UNIX_TIME[[:space:]]*1$/#define BR_USE_UNIX_TIME   0/' \
  -e 's/^#define BR_USE_WIN32_TIME[[:space:]]*1$/#define BR_USE_WIN32_TIME   0/' "$config"
for macro in BR_USE_URANDOM BR_USE_WIN32_RAND BR_USE_UNIX_TIME BR_USE_WIN32_TIME; do
  grep -Eq "^#define ${macro}[[:space:]]+0$" "$config" || {
    echo "Could not disable BearSSL host-only hook: ${macro}" >&2
    exit 1
  }
done
