#!/bin/bash
# Manual wrapper — canonical smoke harness lives in tests/yarsh/smoke.sh
exec "$(cd "$(dirname "$0")/.." && pwd)/tests/yarsh/smoke.sh" "$@"