#!/bin/bash
set -euo pipefail

if [[ ! -x "./lib_install/share/dartvm_embed_lib/dart-sdk/out/ReleaseX64/dart-sdk/bin/dart" ]]; then
  echo "Missing bundled dart. Run ./build_reload.sh first." >&2
  exit 1
fi

SERVICE_URI=${1:-${DARTVM_EMBED_VM_SERVICE_URI:-ws://127.0.0.1:8181/ws}}
ROOT_DIR=$(pwd)
DEFAULT_ROOT_LIB_URI="file://${ROOT_DIR}/example/hello.dart"
DEFAULT_PACKAGES_URI="file://${ROOT_DIR}/example/.dart_tool/package_config.json"

env \
  DARTVM_EMBED_RELOAD_FORCE="${DARTVM_EMBED_RELOAD_FORCE:-1}" \
  DARTVM_EMBED_RELOAD_ROOT_LIB_URI="${DARTVM_EMBED_RELOAD_ROOT_LIB_URI:-$DEFAULT_ROOT_LIB_URI}" \
  DARTVM_EMBED_RELOAD_PACKAGES_URI="${DARTVM_EMBED_RELOAD_PACKAGES_URI:-$DEFAULT_PACKAGES_URI}" \
  ./lib_install/share/dartvm_embed_lib/dart-sdk/out/ReleaseX64/dart-sdk/bin/dart \
  ./tool/reload_sources.dart "$SERVICE_URI"
