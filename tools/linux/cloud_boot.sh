#!/bin/bash
# Boot the Linux build headless in the cloud (lavapipe, Xvfb) for a set time,
# as a check before a Switch test (Mark, 2026-11-07: cloud boot tests until
# the game reaches the title screen). The Linux build shares the Switch's
# thread layer (xboxrecomp/src/platform/win32_compat.c), so lock and
# scheduling bugs show here; speed and feel do not (2 cores, software GPU).
#   tools/linux/cloud_boot.sh [seconds] [KEY=VALUE ...]
# Needs out/build-vk-linux (cmake -DNFSU2_VULKAN=ON -DNFSU2_GEN_DIR=out/gen)
# and the extracted disc in assets/linux_root (ignored, never committed).
# Writes out/cloud/boot.log and a frame every 60 presents to out/cloud/f*.bmp.
set -u
R=$(cd "$(dirname "$0")/../.." && pwd)
SECS=${1:-240}
shift || true
OUT=$R/out/cloud
rm -rf "$OUT"; mkdir -p "$OUT"
BIN=$(ls "$R"/out/build-vk-linux/*_recomp 2>/dev/null | head -1)
[ -x "$BIN" ] || { echo "no Linux build in out/build-vk-linux"; exit 1; }
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
export NFSU2_GAME_DIR=$R/assets/linux_root
export RECOMP_FPS_LOG=1
export RECOMP_GL_THREAD=1          # as on the Switch (src/main.c)
export RECOMP_GIL_EAGER=1
export RECOMP_GL_DUMP=$OUT/f,60
export SDL_AUDIODRIVER=dummy
for kv in "$@"; do export "$kv"; done
cd "$OUT"
# CLOUD_CPUS=0 pins the game to one core: more contention between threads,
# closer to the timing races the Switch hits (test 8f hung there, not here).
PIN=""; [ -n "${CLOUD_CPUS:-}" ] && PIN="taskset -c $CLOUD_CPUS"
# CLOUD_CPU_AT=<s> records each thread's CPU time over the 30 s from that
# point (out/cloud/threads.txt: thread name, seconds of CPU, % of one core).
if [ -n "${CLOUD_CPU_AT:-}" ]; then
  ( sleep "$CLOUD_CPU_AT"; P=$(pgrep -n -f "$(basename "$BIN")")
    snap() { for t in /proc/$P/task/*; do echo "$(basename $t) $(cat $t/comm 2>/dev/null | tr ' ' _) $(awk '{print $14+$15}' $t/stat 2>/dev/null)"; done; }
    snap > "$OUT/t0.txt"; sleep 30; snap > "$OUT/t1.txt"
    join <(sort "$OUT/t0.txt") <(sort "$OUT/t1.txt") | awk -v hz=$(getconf CLK_TCK) '{d=($5-$3)/hz; if (d>0.3) printf "%-8s %-16s %6.1f s  %5.1f%%\n", $1, $2, d, d/30*100}' | sort -k3 -nr > "$OUT/threads.txt" ) &
fi
timeout -s INT "$SECS" $PIN xvfb-run -a -s "-screen 0 1280x720x24" "$BIN" > "$OUT/boot.log" 2>&1
wait
echo "exit $? after up to ${SECS}s; $(ls "$OUT"/f*.bmp 2>/dev/null | wc -l) frames dumped"
grep -E "\[perf\] [0-9.]+ fps|WATCHDOG|CRASH|deadlock" "$OUT/boot.log" | tail -8
