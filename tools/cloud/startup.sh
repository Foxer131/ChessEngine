#!/usr/bin/env bash
# GCP spot VM startup script: build gen_data, generate NNUE self-play data sharded
# across all cores, upload to GCS as it goes, then self-delete. See gcp_datagen.md.
#
# EDIT these two:
BUCKET="gs://CHANGE-ME-chess-nnue"     # your GCS bucket (e.g. gs://joao-chess-nnue)
NET_URL=""                              # optional: gs://.../net.nnue to bootstrap-label with
#
# Tunables:
GAMES_PER_SHARD=15000                   # ~1.4M positions/shard at 5000 nodes
NODES=8000                              # higher than the first runs => cleaner labels
# ---------------------------------------------------------------------------------
set -euo pipefail
exec > >(tee /var/log/datagen.log) 2>&1
echo "=== datagen start $(date) ==="

export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y git cmake ninja-build g++ >/dev/null

# Build only what's needed (no Qt): chess_core + gen_data.
cd /root
git clone --depth 1 https://github.com/Foxer131/ChessEngine.git engine
cd engine
# No Qt on the VM: disable the GUI (and tests) so configure doesn't look for Qt.
cmake -G Ninja -S . -B /root/build -DCMAKE_BUILD_TYPE=Release \
      -DCHESS_BUILD_GUI=OFF -DCHESS_BUILD_TESTS=OFF -DCHESS_BUILD_BENCH=OFF >/dev/null
cmake --build /root/build --target gen_data

EVAL_ARG=""
if [[ -n "$NET_URL" ]]; then
  gcloud storage cp "$NET_URL" /root/net.nnue
  EVAL_ARG="/root/net.nnue"
  echo "bootstrapping: labeling with $NET_URL"
fi

NCORES=$(nproc)
echo "generating on $NCORES cores, $GAMES_PER_SHARD games/shard @ $NODES nodes"
mkdir -p /root/data
pids=()
for i in $(seq 1 "$NCORES"); do
  /root/build/bin/gen_data "/root/data/shard${i}.txt" "$GAMES_PER_SHARD" "$NODES" "$i" $EVAL_ARG &
  pids+=($!)
done
wait "${pids[@]}"

cat /root/data/shard*.txt > /root/data/all.txt
LINES=$(wc -l < /root/data/all.txt)
echo "generated $LINES positions; uploading..."
gcloud storage cp /root/data/all.txt "$BUCKET/all_$(date +%Y%m%d_%H%M%S).txt"
gcloud storage cp /root/data/all.txt "$BUCKET/all.txt"   # convenience 'latest'

echo "=== done $(date); self-deleting ==="
NAME=$(curl -s -H "Metadata-Flavor: Google" http://metadata.google.internal/computeMetadata/v1/instance/name)
ZONE=$(curl -s -H "Metadata-Flavor: Google" http://metadata.google.internal/computeMetadata/v1/instance/zone | awk -F/ '{print $NF}')
gcloud compute instances delete "$NAME" --zone="$ZONE" --quiet
