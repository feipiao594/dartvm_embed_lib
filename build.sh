#!/bin/bash
set -euo pipefail

rm -rf lib_install
rm -rf build

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDARTVM_INSTALL_DARTSDK_BUNDLE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
cmake --build build
cmake --install build --prefix ./lib_install

cd example
rm -rf build
cmake -S . -B build -DDARTVM_EMBED_FLAVOR=jit -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE
cmake --build build

# Stage reload dependencies next to the demo binary so kernel-service can
# resolve platform summaries during reload compilation.
cp -f ../dart-sdk/sdk/out/ReleaseX64/vm_platform_strong.dill ./build/
cd ..

echo "[OK] reload build completed (jit flavor)"
