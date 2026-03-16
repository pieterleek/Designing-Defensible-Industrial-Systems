#!/bin/bash
set -euo pipefail


STEP_CA_URL="${STEP_CA_URL:-https://localhost:9000}"
BROKER_FQDN="broker.factory.local"
BROKER_IP="${BROKER_IP:-192.168.1.34}"
CERT_DIR="./certs"
CA_CERT="../certs/ca.crt"
CA_FINGERPRINT_FILE="../certs/ca-fingerprint.txt"

echo "============================================"
echo "  Broker Certificate Bootstrap"
echo "============================================"

# --- Checks ---
if [ ! -f "$CA_CERT" ]; then
    echo "[FOUT] CA cert niet gevonden op $CA_CERT"
    echo "  Draai eerst: cd ../step-ca && ./setup.sh"
    exit 1
fi

if [ ! -f "$CA_FINGERPRINT_FILE" ]; then
    echo "[FOUT] CA fingerprint niet gevonden op $CA_FINGERPRINT_FILE"
    echo "  Draai eerst: cd ../step-ca && ./setup.sh"
    exit 1
fi

FINGERPRINT=$(cat "$CA_FINGERPRINT_FILE")

# --- Cert directory aanmaken ---
mkdir -p "$CERT_DIR"

# --- CA cert kopiëren ---
echo "[1/3] CA cert kopiëren..."
cp "$CA_CERT" "$CERT_DIR/ca.crt"
echo "  [OK]"

# --- Bootstrap step CLI ---
echo "[2/3] Step CLI bootstrappen..."
step ca bootstrap \
    --ca-url "$STEP_CA_URL" \
    --fingerprint "$FINGERPRINT" \
    --force
echo "  [OK]"

# --- Broker cert aanvragen ---
echo "[3/3] Broker certificaat aanvragen..."
step ca certificate "$BROKER_FQDN" \
    "$CERT_DIR/server.crt" \
    "$CERT_DIR/server.key" \
    --san "$BROKER_FQDN" \
    --san "$BROKER_IP" \
    --ca-url "$STEP_CA_URL" \
    --root "$CA_CERT" \
    --not-after 720h \
    --provisioner "admin"

echo ""
echo "============================================"
echo "  Broker Certs Aangemaakt!"
echo "============================================"
echo ""
echo "  CA:   $CERT_DIR/ca.crt"
echo "  Cert: $CERT_DIR/server.crt"
echo "  Key:  $CERT_DIR/server.key"
echo ""
echo "  SAN: $BROKER_FQDN, $BROKER_IP"
echo ""

# --- Verify ---
echo "  Verificatie:"
openssl x509 -in "$CERT_DIR/server.crt" -noout \
    -subject -issuer -dates -ext subjectAltName 2>/dev/null \
    | sed 's/^/    /'

echo ""
echo "  Start nu Mosquitto met: docker compose up -d"
echo "============================================"
