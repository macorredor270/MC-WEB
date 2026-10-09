#!/usr/bin/env bash
# Mide el dibujo del terreno con recorridos fijos (la cámara da vueltas) y escribe una tabla en markdown.
#
#   tools/bench/render-bench.sh [mcweb] [segundos] [tamaño] [distancias...]
#
# Escenas (semilla 42, creativo, mediodía): "bosque" (en la superficie) y "cueva" (una cueva grande bajo
# tierra, sitio que da `mcweb-bench --find-cave 42`). Con GL por software (llvmpipe) los milisegundos no
# valen para una GPU de verdad, pero sí para comparar antes y después de un cambio.
set -euo pipefail
BIN=${1:-build/linux-release/apps/mcweb/mcweb}
SECS=${2:-8}
SIZE=${3:-480x270}
shift 3 2>/dev/null || shift $# 2>/dev/null || true
DISTS=("$@")
[ ${#DISTS[@]} -eq 0 ] && DISTS=(8 16)
RUN=()
if [ -z "${DISPLAY:-}" ]; then RUN=(xvfb-run -a -s "-screen 0 1280x720x24"); fi

declare -A POS=(["bosque"]="0,90,0" ["cueva"]="-76.5,14,36.5")
echo "| escena | rd | fps | ms media | p95 | p99 | terreno (CPU ms) | GPU ms | llamadas | quads | secciones | oclusión (ms) |"
echo "|---|---|---|---|---|---|---|---|---|---|---|---|"
for scene in bosque cueva; do
  for rd in "${DISTS[@]}"; do
    line=$("${RUN[@]}" "$BIN" --cc0 --seed 42 --rd "$rd" --pos "${POS[$scene]}" --mode creative --time 6000 --freeze-time \
           --no-vsync --size "$SIZE" --bench "$SECS" --bench-spin 2>&1 | grep -F "bench-json:" | sed 's/.*bench-json: //') || true
    if [ -z "$line" ]; then echo "| $scene | $rd | (sin resultado) | | | | | | | | | |"; continue; fi
    python3 - "$scene" "$rd" "$line" <<'PY'
import json, sys
scene, rd, j = sys.argv[1], sys.argv[2], json.loads(sys.argv[3])
gpu = "n/d" if j["gpuMs"] < 0 else f'{j["gpuMs"]:.1f}'
print(f'| {scene} | {rd} | {j["fps"]:.1f} | {j["ms"]["avg"]:.1f} | {j["ms"]["p95"]:.1f} | {j["ms"]["p99"]:.1f} | '
      f'{j["cpu"]["terreno"]:.1f} | {gpu} | {j["draws"]} | {j["quads"]} | {j["sections"]}/{j["sectionsTotal"]} | {j.get("cullMs", 0):.2f} |')
PY
  done
done
