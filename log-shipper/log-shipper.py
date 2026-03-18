#!/usr/bin/env python3
import boto3
import hashlib
import json
import time
import os
import subprocess
from datetime import datetime, timezone

S3_BUCKET = os.environ.get("S3_BUCKET", "qmannexus-audit-logs")
S3_PREFIX = os.environ.get("S3_PREFIX", "mosquitto-logs")
SHIP_INTERVAL = int(os.environ.get("SHIP_INTERVAL", "300"))
AWS_REGION = os.environ.get("AWS_REGION", "eu-central-1")

s3 = boto3.client("s3", region_name=AWS_REGION)

def collect_logs():
    try:
        result = subprocess.run(
            ["docker", "logs", "--since", f"{SHIP_INTERVAL}s", "mosquitto"],
            capture_output=True, text=True, timeout=30)
        return result.stdout + result.stderr
    except Exception as e:
        print(f"[ERROR] Log collection failed: {e}")
        return ""

def ship_to_s3(log_data):
    if not log_data.strip():
        print("[INFO] Geen nieuwe logs, skip upload.")
        return
    now = datetime.now(timezone.utc)
    timestamp = now.strftime("%Y-%m-%dT%H-%M-%S")
    date_prefix = now.strftime("%Y/%m/%d")
    log_hash = hashlib.sha256(log_data.encode()).hexdigest()
    envelope = {
        "source": "mosquitto",
        "timestamp": now.isoformat(),
        "sha256": log_hash,
        "line_count": len(log_data.strip().split("\n")),
        "logs": log_data,
    }
    key = f"{S3_PREFIX}/{date_prefix}/{timestamp}_{log_hash[:12]}.json"
    try:
        s3.put_object(
            Bucket=S3_BUCKET, Key=key,
            Body=json.dumps(envelope, indent=2),
            ContentType="application/json",
            Metadata={"sha256": log_hash, "source": "mosquitto"})
        print(f"[OK] Shipped {envelope['line_count']} lines -> s3://{S3_BUCKET}/{key}")
        print(f"     SHA-256: {log_hash}")
    except Exception as e:
        print(f"[ERROR] S3 upload failed: {e}")

def main():
    print("============================================")
    print("  QmanNexus Log Shipper")
    print(f"  Bucket:   {S3_BUCKET}")
    print(f"  Interval: {SHIP_INTERVAL}s")
    print("============================================")
    while True:
        print(f"\n[{datetime.now(timezone.utc).isoformat()}] Collecting logs...")
        ship_to_s3(collect_logs())
        time.sleep(SHIP_INTERVAL)

if __name__ == "__main__":
    main()
