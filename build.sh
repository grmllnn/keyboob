#!/usr/bin/env bash
# Compat shim — prefer `make`.
exec make -C "$(dirname "$0")" "$@"
