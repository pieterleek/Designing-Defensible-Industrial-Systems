#!/bin/bash
set -euo pipefail

STEP_CA_URL="${STEP_CA_URL:-https://localhost:9000}"
DEVICE_ID="${1:-FlowMeter-01}"
CERT_DIR="./data"
CA_CERT="../certs/ca.crt"
CA_FINGERPRINT_FILE="../certs/ca-fingerprint.txt"

echo "============================================"
echo "  ESP32 Device Provisioning: $DEVICE_ID"
echo "============================================"

# --- Checks ---
if [ ! -f "$CA_CERT" ]; then
    echo "[FOUT] CA cert niet gevonden op $CA_CERT"
    echo "  Draai eerst: cd ../step-ca && ./setup.sh"
    exit 1
fi

if [ ! -f "$CA_FINGERPRINT_FILE" ]; then
    echo "[FOUT] CA fingerprint niet gevonden."
    exit 1
fi

FINGERPRINT=$(cat "$CA_FINGERPRINT_FILE")

# --- Cert directory aanmaken ---
mkdir -p "$CERT_DIR"

# --- Bootstrap step CLI ---
echo "[1/4] Step CLI bootstrappen..."
step ca bootstrap \
    --ca-url "$STEP_CA_URL" \
    --fingerprint "$FINGERPRINT" \
    --force
echo "  [OK]"

# --- CA cert kopiëren ---
echo "[2/4] CA cert kopiëren naar data/..."
cp "$CA_CERT" "$CERT_DIR/ca.crt"
echo "  [OK]"

# --- Client cert aanvragen ---
echo "[3/4] Client certificaat aanvragen voor '$DEVICE_ID'..."
step ca certificate "$DEVICE_ID" \
    "$CERT_DIR/esp32.crt" \
    "$CERT_DIR/esp32.key" \
    --ca-url "$STEP_CA_URL" \
    --root "$CA_CERT" \
    --not-after 8760h \
    --provisioner "admin"

echo "  [OK]"

# --- Verify ---
echo "[4/4] Verificatie..."
echo ""
openssl x509 -in "$CERT_DIR/esp32.crt" -noout \
    -subject -issuer -dates 2>/dev/null \
    | sed 's/^/    /'

echo ""
echo "============================================"
echo "  Device '$DEVICE_ID' Geprovisioned!"
echo "============================================"
echo ""
echo "  Bestanden in $CERT_DIR/:"
ls -la "$CERT_DIR/"
echo ""
echo "  Upload naar ESP32 met:"
echo "    pio run --target uploadfs"
echo "    pio run --target upload"
echo "============================================"