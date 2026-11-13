#!/bin/bash
set -e
cd "$(dirname "$0")"
python3 gen.py
T="--target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -mfloat-abi=softfp -mfpu=vfpv3-d16"
echo 'void* dlopen(const char* a,int b){return 0;} void* dlsym(void* a,const char* b){return 0;}' > stub_dl.c
clang $T -fPIC -shared -nostdlib -fuse-ld=lld -Wl,-soname,libdl.so -o libdl.so stub_dl.c
clang $T -fPIC -shared -nostdlib -O2 -fno-stack-protector -fno-builtin -ffreestanding -fvisibility=hidden -fuse-ld=lld \
  -Wl,-soname,libpw2gl.so -Wl,--no-undefined -Wl,-z,max-page-size=4096 -Wl,--hash-style=both \
  -o libpw2gl.so shim.c libdl.so
echo built libpw2gl.so
