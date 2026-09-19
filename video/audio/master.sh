#!/bin/sh
# Master the rendered film's audio: two-pass EBU R128 to -16 LUFS with 1.5 dB of
# true-peak headroom, which is the web target. The video stream is copied, so
# this costs nothing and is not a re-encode.
#
# Two passes because loudnorm's single-pass mode is a live estimator and
# overshoots; the first pass measures the file and the second applies it.
set -e
cd "$(dirname "$0")/.."
RAW=out/raw.mp4
OUT=out/inkagent-launch.mp4
[ -f "$RAW" ] || { echo "no $RAW — run npm run render first" >&2; exit 1; }

M=$(ffmpeg -hide_banner -nostats -i "$RAW" \
      -af loudnorm=I=-16:TP=-1.5:LRA=11:print_format=json -f null - 2>&1 |
    awk '/^\{/{f=1} f')
get() { printf '%s' "$M" | sed -n "s/.*\"$1\" *: *\"\([^\"]*\)\".*/\1/p"; }

ffmpeg -hide_banner -loglevel error -y -i "$RAW" -c:v copy \
  -af "loudnorm=I=-16:TP=-1.5:LRA=11:measured_I=$(get input_i):measured_TP=$(get input_tp):measured_LRA=$(get input_lra):measured_thresh=$(get input_thresh):offset=$(get target_offset):linear=true" \
  -ar 48000 -c:a aac -b:a 192k "$OUT"

ffmpeg -hide_banner -nostats -i "$OUT" -af ebur128=peak=true -f null - 2>&1 |
  grep -A4 'Integrated loudness\|True peak' | grep -E 'I:|Peak:'
echo "mastered -> $OUT"
