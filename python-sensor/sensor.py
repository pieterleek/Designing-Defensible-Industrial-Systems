#!/usr/bin/env python3
"""
FlowMeter-01 Sensor Simulator
Simuleert de ESP32 MQTT client voor het testen van de broker.

Gebruik:
    pip install paho-mqtt
    python simulator.py
"""

import json
import time
import random
import ssl
import sys
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Installeer eerst: pip install paho-mqtt")
    sys.exit(1)

# --- CONFIGURATIE ---
BROKER_IP = "127.0.0.1"
BROKER_PORT = 8883
DEVICE_ID = "FlowMeter-01"

# ISA-95 Unified Namespace
TOPIC_ROOT = "QmanNexus/Edam/Processing/Line1/CellA"
TOPIC_DATA = f"{TOPIC_ROOT}/{DEVICE_ID}/DATA"
TOPIC_STATUS = f"{TOPIC_ROOT}/{DEVICE_ID}/STATUS"
TOPIC_META = f"{TOPIC_ROOT}/{DEVICE_ID}/META"

# Certificaten (pas paden aan indien nodig)
CA_CERT = "../broker/certs/ca.crt"
CLIENT_CERT = "../esp32/data/esp32.crt"
CLIENT_KEY = "../esp32/data/esp32.key"

# Interval in seconden
DATA_INTERVAL = 5

# --- TLS (mTLS of zonder client cert) ---
USE_MTLS = True


def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"[MQTT] Verbonden met broker op {BROKER_IP}:{BROKER_PORT}")

        # Online status (retained)
        client.publish(TOPIC_STATUS, "ONLINE", qos=1, retain=True)

        # Birth certificate
        meta = {
            "device": DEVICE_ID,
            "ip": "simulator",
            "fw_version": "sim-1.0.0",
            "boot_time": int(time.time()),
        }
        client.publish(TOPIC_META, json.dumps(meta), qos=1, retain=True)
        print(f"[MQTT] Birth certificate gepubliceerd op {TOPIC_META}")
    else:
        errors = {
            1: "BAD_PROTOCOL",
            2: "BAD_CLIENT_ID",
            3: "UNAVAILABLE",
            4: "BAD_CREDENTIALS",
            5: "UNAUTHORIZED",
        }
        print(f"[MQTT] Verbinding mislukt: rc={rc} ({errors.get(rc, 'UNKNOWN')})")


def on_disconnect(client, userdata, rc, properties=None):
    print(f"[MQTT] Verbinding verloren (rc={rc})")


def main():
    print("========================================")
    print(f"  Sensor Simulator - {DEVICE_ID}")
    print("========================================")

    client = mqtt.Client(
        client_id=DEVICE_ID,
        protocol=mqtt.MQTTv311,
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
    )

    # TLS configuratie
    if USE_MTLS:
        print(f"[TLS] mTLS met client cert: {CLIENT_CERT}")
        client.tls_set(
            ca_certs=CA_CERT,
            certfile=CLIENT_CERT,
            keyfile=CLIENT_KEY,
            tls_version=ssl.PROTOCOL_TLSv1_2,
        )
    else:
        print("[TLS] Zonder client cert (server-only TLS)")
        client.tls_set(
            ca_certs=CA_CERT,
            tls_version=ssl.PROTOCOL_TLSv1_2,
        )
        # Uncomment om cert verificatie te skippen (testen):
       # client.tls_insecure_set(True)

    # Last Will & Testament
    client.will_set(TOPIC_STATUS, "OFFLINE", qos=1, retain=True)


    client.on_connect = on_connect
    client.on_disconnect = on_disconnect

    try:
        print(f"[MQTT] Verbinden met {BROKER_IP}:{BROKER_PORT}...")
        client.connect(BROKER_IP, BROKER_PORT, keepalive=60)
        client.loop_start()

        while True:
            if client.is_connected():
                flow = round(random.uniform(20.0, 45.0), 1)
                temp = round(random.uniform(18.0, 22.0), 1)

                payload = {
                    "timestamp": int(time.time()),
                    "FlowRate": flow,
                    "Temperature": temp,
                }

                client.publish(TOPIC_DATA, json.dumps(payload), qos=1)
                now = datetime.now().strftime("%H:%M:%S")
                print(f"[{now}] TX: Flow={flow} L/min, Temp={temp}°C")

            time.sleep(DATA_INTERVAL)

    except KeyboardInterrupt:
        print("\n[STOP] Simulator gestopt.")
        client.publish(TOPIC_STATUS, "OFFLINE", qos=1, retain=True)
        client.disconnect()
        client.loop_stop()


if __name__ == "__main__":
    main()