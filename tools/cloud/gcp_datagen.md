# Cloud data generation (GCP spot) — NNUE training data

Generate NNUE self-play data on a cheap **GCP spot (preemptible)** VM, upload to a
bucket, train **locally** on the RTX 4050. Training is minutes and free locally;
only CPU generation goes to the cloud. See `docs/NNUE.md` for the why and the
bootstrapping loop (label with the current net to escalate past HCE).

## Cost estimate (confirm current GCP prices; ~mid-2026, us-central1)
- **c2d-standard-32** (32 vCPU AMD) spot ≈ **$0.30–0.45/h**. At ~289 pos/s/thread
  that's ~9k pos/s × 32 ≈ **~1M pos/min ⇒ ~50M positions in ~50 min ≈ $0.30–0.40**.
- **c2d-standard-60** (60 vCPU) spot ≈ **$0.55–0.85/h** ⇒ 50M in ~25 min.
- **Storage** (GCS standard): 50M positions in text ≈ ~3 GB ≈ **$0.06/GB-month**;
  delete after downloading. Egress to download ~3 GB ≈ **~$0.30** (first GB free).
- **Per 50M-position batch: well under $1.** A full bootstrap round (generate →
  download → train local → SPRT) is a few cents of compute + your time.

> Spot VMs can be preempted; the startup script writes shards incrementally and
> uploads as it goes, so a preemption only loses the in-flight shard.

## One-time setup (your machine, your GCP account)
1. Install the `gcloud` CLI and `gcloud init` (pick a project + region).
2. Create a bucket: `gcloud storage buckets create gs://<YOURNAME>-chess-nnue`.
3. Edit the two `<...>` placeholders in `startup.sh` (bucket, and optionally the
   net URL for bootstrapping).

## Launch a burst (auto-generates, uploads, self-deletes)
```bash
gcloud compute instances create chess-datagen \
  --zone=us-central1-a \
  --machine-type=c2d-standard-32 \
  --provisioning-model=SPOT \
  --instance-termination-action=DELETE \
  --image-family=debian-12 --image-project=debian-cloud \
  --metadata-from-file=startup-script=tools/cloud/startup.sh
```
The VM runs `startup.sh`, generates the dataset, uploads it to the bucket, then
deletes itself. Watch progress: `gcloud compute instances get-serial-port-output
chess-datagen --zone=us-central1-a`.

## Download + train locally
```powershell
gcloud storage cp gs://<YOURNAME>-chess-nnue/all.txt C:\chess_sprt\data\all.txt
powershell -File tools\training\retrain.ps1        # normalize -> convert -> train
# then: copy checkpoints\chessengine-*\quantised.bin to a net.nnue and SPRT
powershell -File tools\training\sprt_nnue.ps1 -Net C:\chess_sprt\data\net.nnue -Nodes 20000
```

## Bootstrapping loop (the actual strength escalator)
After a net beats the previous one, upload it to the bucket and pass its URL to the
next generation so the cloud labels with it (`startup.sh` `NET_URL`):
  gen v1 (HCE) → train → gen v2 (label with v1) → train → gen v3 (label with v2) → …
Each round's targets come from a stronger teacher. Stop when SPRT gains plateau.
