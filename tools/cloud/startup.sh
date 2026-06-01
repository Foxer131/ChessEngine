#!/usr/bin/env bash
# GCP spot VM startup script: build gen_data, generate NNUE self-play data sharded
# across all cores, upload to GCS as it goes, then self-delete. See gcp_datagen.md.
#
# EDIT these two:
BUCKET="gs://whoisthisfrom-chess-nnue"     # your GCS bucket (e.g. gs://joao-chess-nnue)
NET_URL=""                              # optional: gs://.../net.nnue to bootstrap-label with
#
# Tunables:
GAMES_PER_SHARD=4000                    # per core. 4000 x 32 cores @8000 nodes ~= 12M pos, ~35 min
NODES=8000                              # higher than the first runs => cleaner labels
# ---------------------------------------------------------------------------------
# NOTE: deliberately NOT using `set -e`/`pipefail` or `exec | tee`. A long parallel
# job + a tee pipe can hand a worker SIGPIPE (exit 141), and with pipefail+`set -e`
# that aborts the whole script mid-run (we lost a ~1h run that way). The startup
# runner already captures stdout to the serial log, so we just write plainly and
# guard the critical steps by hand.
set -u
echo "=== datagen start $(date) ==="

export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y git cmake ninja-build g++ || { echo "apt failed"; }

# Build only what's needed (no Qt): chess_core + gen_data.
cd /root
git clone --depth 1 https://github.com/Foxer131/ChessEngine.git engine
cd engine
# No Qt on the VM: disable the GUI (and tests) so configure doesn't look for Qt.
cmake -G Ninja -S . -B /root/build -DCMAKE_BUILD_TYPE=Release \
      -DCHESS_BUILD_GUI=OFF -DCHESS_BUILD_TESTS=OFF -DCHESS_BUILD_BENCH=OFF >/dev/null
cmake --build /root/build --target gen_data
if [[ ! -x /root/build/bin/gen_data ]]; then
  echo "BUILD FAILED: gen_data missing - aborting (VM left running for inspection)"; exit 1
fi

EVAL_ARG=""
if [[ -n "$NET_URL" ]]; then
  gcloud storage cp "$NET_URL" /root/net.nnue
  EVAL_ARG="/root/net.nnue"
  echo "bootstrapping: labeling with $NET_URL"
fi

NCORES=$(nproc)
RUN_ID="$(date +%Y%m%d_%H%M%S)"
echo "generating on $NCORES cores, $GAMES_PER_SHARD games/shard @ $NODES nodes (run $RUN_ID)"
mkdir -p /root/data

# Each shard uploads its OWN file to the bucket as soon as it finishes, so a spot
# preemption only loses the shards still in flight (not the whole run). We then
# also concatenate the local shards into a convenience all.txt at the end.
gen_and_upload() {
  local i="$1"
  # Send the worker's progress (stderr) to a per-shard file, NOT the shared serial
  # pipe - that pipe is what caused SIGPIPE/exit-141 and killed a whole run.
  /root/build/bin/gen_data "/root/data/shard${i}.txt" "$GAMES_PER_SHARD" "$NODES" "$i" $EVAL_ARG \
      > "/root/data/shard${i}.log" 2>&1
  gcloud storage cp "/root/data/shard${i}.txt" "$BUCKET/${RUN_ID}/shard${i}.txt"
  echo "shard ${i} done + uploaded"
}
pids=()
for i in $(seq 1 "$NCORES"); do gen_and_upload "$i" & pids+=($!); done
wait "${pids[@]}"

cat /root/data/shard*.txt > /root/data/all.txt
LINES=$(wc -l < /root/data/all.txt)
echo "generated $LINES positions; uploading merged all.txt..."
gcloud storage cp /root/data/all.txt "$BUCKET/all_${RUN_ID}.txt"
gcloud storage cp /root/data/all.txt "$BUCKET/all.txt"   # convenience 'latest'

echo "=== done $(date); shutting down ==="
# Try to self-delete via the API (needs the compute scope; our VM only has
# storage-rw, so this usually fails - that's fine). EITHER WAY, halt the machine:
# `shutdown -h` always works and a STOPPED instance costs ~nothing (only its small
# disk). To self-DELETE without manual cleanup, launch with
# `--scopes=storage-rw,compute-rw` (then the delete below succeeds). Otherwise
# just `gcloud compute instances delete chess-datagen --zone=... --quiet` by hand.
NAME=$(curl -s -H "Metadata-Flavor: Google" http://metadata.google.internal/computeMetadata/v1/instance/name)
ZONE=$(curl -s -H "Metadata-Flavor: Google" http://metadata.google.internal/computeMetadata/v1/instance/zone | awk -F/ '{print $NF}')
gcloud compute instances delete "$NAME" --zone="$ZONE" --quiet || shutdown -h now
