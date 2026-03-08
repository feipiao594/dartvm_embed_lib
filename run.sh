#!/bin/bash
set -euo pipefail

export DARTVM_EMBED_HOT_RELOAD=${DARTVM_EMBED_HOT_RELOAD:-1}
export DARTVM_EMBED_VM_SERVICE_PORT=${DARTVM_EMBED_VM_SERVICE_PORT:-8181}

cd example/build
./external_consumer_demo
