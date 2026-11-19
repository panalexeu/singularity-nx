#!/usr/bin/env bash
# Profile-guided optimisation, step 2 of 3 (from renut_nx_ultimate_base's tools/pgo_merge.py, deadpool-nx).
#
# Step 1: PGO=generate tools/switch/build-native.sh, then play that NRO: it writes pgo/singularity_pgo_<N>.stream next to
# itself on the SD card (switch/src/singularity_pgo.cpp). Copy those .stream files to a folder on the PC and run
#
#   tools/switch/pgo_merge.sh <folder with the .stream files>
#
# Each stream's counts are added into the .gcda files of pgo/ (gcov-tool merge-stream, in the build's Docker image:
# the streams name /src/pgo, the repository's pgo/ there), then their summaries are filled in (pgo_summary.py: the
# stream writes them as zero). Step 3: PGO=use tools/switch/build-native.sh.
#
# merge-stream ADDS to .gcda files already there: this stops when pgo/ already has some (move them aside first). Only
# streams of the SAME instrumented build may be merged (functions are matched by checksum).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
streams="$(cd "${1:?usage: $0 <folder with singularity_pgo_*.stream>}" && pwd)"
ls "$streams"/singularity_pgo_*.stream >/dev/null 2>&1 || { echo "no singularity_pgo_*.stream in $streams" >&2; exit 1; }
mkdir -p "$root/pgo"
if ls "$root/pgo"/*.gcda >/dev/null 2>&1; then
  echo "$root/pgo already has .gcda files: move them aside first (merge-stream adds to them)" >&2; exit 1
fi
n=$(ls "$streams"/singularity_pgo_*.stream | wc -l)
docker run --rm -u "$(id -u):$(id -g)" -v "$root:/src" -v "$streams:/streams:ro" singularity-switch bash -c '
  set -e
  for s in /streams/singularity_pgo_*.stream; do
    echo "merging $(basename "$s") ($(du -h "$s" | cut -f1))"
    /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcov-tool merge-stream "$s"
  done'
count=$(ls "$root/pgo"/*.gcda 2>/dev/null | wc -l)
[ "$count" -gt 0 ] || { echo "no .gcda in pgo/ after merging (streams from another build?)" >&2; exit 1; }
python3 "$root/tools/switch/pgo_summary.py" "$root/pgo" "$n"
echo "$n session(s) merged into $count .gcda files in pgo/; now: PGO=use tools/switch/build-native.sh"
