#!/bin/sh
# The translator over the game's own shaders (tools/test_translate_corpus.c): every vertex, fragment and compute binary
# of the RomFS's shader archives translated to GLSL with the switches in the environment, each compiled with
# glslangValidator (Vulkan 1.3, as runtime/nvn/vulkan/spirv.c's shaderc).
# usage: [SAVAGEWIND_...=...] [TT_GMEM_MIRROR=1] tools/test_translate_corpus.sh <extracted/romfs folder> [name] [jobs]
# The GLSL goes to build/test_corpus/<name>/ (default: "run"); exits 1 when any translation doesn't compile.
# Needs clang and glslangValidator.
set -e
cd "$(dirname "$0")/.."
ROMFS=${1:?usage: tools/test_translate_corpus.sh <romfs folder> [name] [jobs]}
NAME=${2:-run}
JOBS=${3:-8}
OUT=build/test_corpus
CC=${CC:-clang}  # (runtime headers use clang builtins, as tools/test_translate.sh)
mkdir -p "$OUT/$NAME"
rm -f "$OUT/$NAME"/*.vert "$OUT/$NAME"/*.frag "$OUT/$NAME"/*.comp "$OUT/$NAME"/glslang.log
DEFS=""
grep -q maxwell_const_data runtime/nvn/maxwell/shader.h && DEFS="-DHAVE_CONST_DATA"
grep -q MAXWELL_FLOW_CHECK runtime/nvn/maxwell/translate.c && DEFS="$DEFS -DMAXWELL_FLOW_CHECK"
grep -q maxwell_gmem_mirror runtime/nvn/maxwell/shader.h && DEFS="$DEFS -DHAVE_GMEM_MIRROR"
$CC -O2 -w $DEFS -Iruntime -Iruntime/nvn tools/test_translate_corpus.c runtime/nvn/maxwell/translate.c \
  runtime/nvn/shader_archive.c runtime/romfs_image.c runtime/romfs_build.c -o "$OUT/test_translate_corpus" -lpthread -lm
"$OUT/test_translate_corpus" "$ROMFS" "$OUT/$NAME" 2> "$OUT/$NAME/stderr.txt" | tee "$OUT/$NAME/summary.txt"
FB=$(grep -c '^\[flow check\]' "$OUT/$NAME/stderr.txt" || true)
# each file: "ok" or its errors
ls "$OUT/$NAME" | grep -E '\.(vert|frag|comp)$' | xargs -P "$JOBS" -I{} sh -c \
  'if glslangValidator -V --target-env vulkan1.3 "$0/{}" -o /dev/null > "$0/{}.log" 2>&1; then rm -f "$0/{}.log"; else echo "{}"; fi' \
  "$OUT/$NAME" > "$OUT/$NAME/glslang_failed.txt" || true
NF=$(wc -l < "$OUT/$NAME/glslang_failed.txt")
NT=$(ls "$OUT/$NAME" | grep -cE '\.(vert|frag|comp)$')
echo "glslang: $NT translations, $NF failed to compile (logs: $OUT/$NAME/<file>.log)" | tee -a "$OUT/$NAME/summary.txt"
[ "$NF" -eq 0 ] && [ "$FB" -eq 0 ]
