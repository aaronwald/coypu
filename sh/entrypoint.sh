#!/bin/bash
set -e

export LD_LIBRARY_PATH=/usr/local/lib/:/opt/coypu/

exec "$@"


