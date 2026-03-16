#!/bin/bash
set -euo pipefail

STEP_CA_URL="https://localhost:9000"
CONTAINER="step-ca"

echo "============================================"
echo "  Step-CA Post-Init Setup"
echo "============================================"

# --- 1. Wacht tot step-ca draait ---
echo "[1/5] Wachten op step-ca..."
until docker exec $CONTAINER step ca health --ca-url $STEP_CA_URL --root /home/step/certs/root_ca.crt 2>/dev/null; do
    sleep 2
    echo "  Nog niet klaar..."
done
echo "  [OK] Step-CA is online."

# --- 2. Haal het CA root certificaat op ---
echo "[2/5] Root CA certificaat ophalen..."
mkdir -p ../certs
docker cp $CONTAINER:/home/step/certs/root_ca.crt ../certs/ca.crt
echo "  [OK] Opgeslagen in ../certs/ca.crt"

# --- 3. Haal CA fingerprint op ---
echo "[3/5] CA fingerprint ophalen..."
FINGERPRINT=$(docker exec $CONTAINER step certificate fingerprint /home/step/certs/root_ca.crt)
echo "  Fingerprint: $FINGERPRINT"
echo "$FINGERPRINT" > ../certs/ca-fingerprint.txt
echo "  [OK] Opgeslagen in ../certs/ca-fingerprint.txt"

# --- 4. Voeg JWK provisioner toe voor ESP32 devices ---
echo "[4/5] JWK provisioner toevoegen voor ESP32 devices..."
# Haal admin provisioner wachtwoord op (werd gegenereerd bij init)
echo ""
echo "  Je hebt het admin-wachtwoord nodig dat bij de eerste start"
echo "  van step-ca is getoond in de logs."
echo ""
echo "  Bekijk het met: docker logs $CONTAINER 2>&1 | grep 'password'"
echo ""
read -sp "  Admin wachtwoord: " ADMIN_PASS
echo ""

docker exec -e ADMIN_PASS="$ADMIN_PASS" $CONTAINER sh -c '
    echo "$ADMIN_PASS" | step ca provisioner add esp32-provisioner \
        --type JWK \
        --create \
        --password-file /dev/stdin \
        --ca-url https://localhost:9000 \
        --root /home/step/certs/root_ca.crt \
        --admin-provisioner "Admin JWK" \
        --admin-password-file /dev/stdin
' && echo "  [OK] esp32-provisioner aangemaakt." \
  || echo "  [!] Provisioner bestaat mogelijk al, check handmatig."

# --- 5. Samenvatting ---
echo ""
echo "============================================"
echo "  Setup Compleet!"
echo "============================================"
echo ""
echo "  CA URL:         $STEP_CA_URL"
echo "  CA Fingerprint: $FINGERPRINT"
echo "  CA Cert:        ../certs/ca.crt"
echo ""
echo "  Provisioners:"
echo "    - ACME (ingebouwd)  → voor broker cert renewal"
echo "    - esp32-provisioner → voor ESP32 client certs"
echo ""
echo "  Volgende stappen:"
echo "    1. cd ../broker && ./bootstrap-certs.sh"
echo "    2. cd ../esp32  && ./provision-device.sh"
echo "============================================"
