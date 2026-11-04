#!/bin/sh
# Compiles a pixel shader of the mod into a C++ header, with Wine's own
# d3dcompiler_47, in a throwaway prefix: the build has no shader compiler and
# the result is checked in beside its source.
#   compile.sh <shader.hlsl> <output.h> <array name>
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
i686-w64-mingw32-gcc -O1 "$here/hlslc.c" -o "$work/hlslc.exe"
cp "$1" "$work/shader.hlsl"
(cd "$work" && WINEPREFIX="$work/prefix" WINEDEBUG=-all wine ./hlslc.exe shader.hlsl main ps_2_0 shader.bin) 2>/dev/null
python3 -I - "$work/shader.bin" "$2" "$3" "$(basename "$1")" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
words = struct.unpack("<%dI" % (len(data) // 4), data)
out = ["#pragma once", "", "#include <cstdint>", "", "// Compiled from " + sys.argv[4] + " by compile.sh, ps_2_0", "",
       "namespace dxvk {", "", "  constexpr uint32_t " + sys.argv[3] + "[] = {"]
for i in range(0, len(words), 8):
    out.append("    " + ", ".join("0x%08x" % w for w in words[i:i + 8]) + ",")
out += ["  };", "", "}", ""]
open(sys.argv[2], "w").write("\n".join(out))
PY
