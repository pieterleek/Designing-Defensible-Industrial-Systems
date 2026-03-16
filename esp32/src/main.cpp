#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>


#include "config.h"

// --- TOPICS (gebouwd vanuit config) ---
String topic_data;
String topic_status;
String topic_meta;

// --- NETWORK CLIENTS ---
WiFiClientSecure espClient;
PubSubClient client(espClient);

// --- TIMING (Non-blocking) ---
unsigned long lastMsg = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long DATA_INTERVAL      = 5000;
const unsigned long RECONNECT_INTERVAL = 5000;

// --- WATCHDOG ---
const uint32_t WDT_TIMEOUT_SEC = 30;

// --- CERTIFICATEN (Heap) ---
String ca_str, cert_str, key_str;

// ============================================================
//  WIFI
// ============================================================
void connectToWiFi() {
    Serial.printf("[WiFi] Verbinden met '%s'... ", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    int retries = 0;
    const int maxRetries = 40; // 20 seconden
    while (WiFi.status() != WL_CONNECTED && retries < maxRetries) {
        delay(500);
        Serial.print(".");
        retries++;
        esp_task_wdt_reset();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(" [OK] IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(" [MISLUKT] Herstart in 10s...");
        delay(10000);
        ESP.restart();
    }
}

// ============================================================
//  NTP 
// ============================================================
void syncTime() {
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
    Serial.print("[NTP] Tijd syncen: ");

    int retries = 0;
    const int maxRetries = 100;
    while (time(nullptr) < 1000000000l && retries < maxRetries) {
        delay(100);
        retries++;
        Serial.print(".");
    }

    if (retries >= maxRetries) {
        Serial.println(" [TIMEOUT] Draait zonder nauwkeurige tijd.");
    } else {
        time_t now = time(nullptr);
        Serial.printf(" [OK] %s", ctime(&now));
    }
}

// ============================================================
//  Mtls
// ============================================================
bool loadCertificates() {
    if (!LittleFS.begin(true)) {
        Serial.println("[CERT] LittleFS mount MISLUKT!");
        return false;
    }
    Serial.println("[CERT] LittleFS gemount.");

    // --- CA Certificate ---
    File ca = LittleFS.open("/ca.crt", "r");
    if (!ca) {
        Serial.println("[CERT] FOUT: /ca.crt niet gevonden!");
        return false;
    }
    ca_str = ca.readString();
    ca.close();
    Serial.printf("[CERT] ca.crt geladen (%d bytes)\n", ca_str.length());

    // --- Client Certificate ---
    File cert = LittleFS.open("/esp32.crt", "r");
    if (!cert) {
        Serial.println("[CERT] FOUT: /esp32.crt niet gevonden!");
        return false;
    }
    cert_str = cert.readString();
    cert.close();
    Serial.printf("[CERT] esp32.crt geladen (%d bytes)\n", cert_str.length());

    // --- Client Private Key ---
    File key = LittleFS.open("/esp32.key", "r");
    if (!key) {
        Serial.println("[CERT] FOUT: /esp32.key niet gevonden!");
        return false;
    }
    key_str = key.readString();
    key.close();
    Serial.printf("[CERT] esp32.key geladen (%d bytes)\n", key_str.length());

    // Certificaten toewijzen aan TLS client
    espClient.setCACert(ca_str.c_str());
    espClient.setCertificate(cert_str.c_str());
    espClient.setPrivateKey(key_str.c_str());

    Serial.println("[CERT] mTLS geconfigureerd.");
    return true;
}

// ============================================================
//  MQTT VERBINDING + LWT + BIRTH CERTIFICATE
// ============================================================
void publishBirthCertificate() {
    StaticJsonDocument<512> doc;
    doc["device"]     = device_id;
    doc["ip"]         = WiFi.localIP().toString();
    doc["mac"]        = WiFi.macAddress();
    doc["fw_version"] = fw_version;
    doc["boot_time"]  = time(nullptr);
    doc["rssi"]       = WiFi.RSSI();

    char buffer[512];
    serializeJson(doc, buffer);
    client.publish(topic_meta.c_str(), buffer, true);
    Serial.printf("[MQTT] Birth certificate gepubliceerd op %s\n", topic_meta.c_str());
}

bool connectToMqtt() {
    Serial.print("[MQTT] Verbinden (mTLS)... ");

    if (client.connect(device_id, topic_status.c_str(), 1, true, "OFFLINE")) {
        Serial.println("[VERBONDEN]");

        client.publish(topic_status.c_str(), "ONLINE", true);
        publishBirthCertificate();

        return true;
    } else {
        Serial.printf("[MISLUKT] rc=%d\n", client.state());
        switch (client.state()) {
            case -4: Serial.println("  -> MQTT_CONNECTION_TIMEOUT"); break;
            case -3: Serial.println("  -> MQTT_CONNECTION_LOST"); break;
            case -2: Serial.println("  -> MQTT_CONNECT_FAILED"); break;
            case -1: Serial.println("  -> MQTT_DISCONNECTED"); break;
            case  1: Serial.println("  -> MQTT_CONNECT_BAD_PROTOCOL"); break;
            case  2: Serial.println("  -> MQTT_CONNECT_BAD_CLIENT_ID"); break;
            case  3: Serial.println("  -> MQTT_CONNECT_UNAVAILABLE"); break;
            case  4: Serial.println("  -> MQTT_CONNECT_BAD_CREDENTIALS"); break;
            case  5: Serial.println("  -> MQTT_CONNECT_UNAUTHORIZED"); break;
        }
        return false;
    }
}

// ============================================================
//  SENSOR DATA PUBLICEREN
// ============================================================
void publishSensorData() {
    // TODO: Vervang door echte sensor reads
    float flow = random(200, 450) / 10.0;
    float temp = random(180, 220) / 10.0;

    StaticJsonDocument<256> doc;
    doc["timestamp"]   = time(nullptr);
    doc["FlowRate"]    = flow;
    doc["Temperature"] = temp;

    char buffer[256];
    size_t len = serializeJson(doc, buffer);

    if (client.beginPublish(topic_data.c_str(), len, false)) {
        client.print(buffer);
        client.endPublish();
        Serial.printf("[TX] %s\n", buffer);
    } else {
        Serial.println("[TX] Publish MISLUKT!");
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n========================================");
    Serial.printf( "  QmanNexus - %s v%s\n", device_id, fw_version);
    Serial.println("========================================");

    // Watchdog
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.printf("[WDT] Watchdog actief (%ds timeout)\n", WDT_TIMEOUT_SEC);

    // Random seed
    randomSeed(analogRead(0));

    // Topics opbouwen
    topic_data   = String(topic_root) + "/" + device_id + "/DATA";
    topic_status = String(topic_root) + "/" + device_id + "/STATUS";
    topic_meta   = String(topic_root) + "/" + device_id + "/META";

    Serial.printf("[TOPIC] Data:   %s\n", topic_data.c_str());
    Serial.printf("[TOPIC] Status: %s\n", topic_status.c_str());
    Serial.printf("[TOPIC] Meta:   %s\n", topic_meta.c_str());

    // WiFi
    connectToWiFi();

    // NTP
    syncTime();

    // Certificaten
    if (!loadCertificates()) {
        Serial.println("[FATAL] Certificaten niet geladen. Herstart in 10s...");
        delay(10000);
        ESP.restart();
    }

    // MQTT
    // espClient.setInsecure();
    client.setServer(mqtt_server, mqtt_port);
    client.setBufferSize(1024);

    connectToMqtt();

    Serial.println("[SETUP] Initialisatie compleet.\n");
}

// ============================================================
//  MAIN LOOP
// ============================================================
void loop() {
    esp_task_wdt_reset();

    // WiFi check
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Verbinding verloren! Reconnect...");
        WiFi.reconnect();
        delay(5000);
        esp_task_wdt_reset();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Nog steeds geen verbinding.");
            return;
        }
        Serial.printf("[WiFi] Opnieuw verbonden. IP: %s\n",
            WiFi.localIP().toString().c_str());
    }

    // MQTT check
    if (!client.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            connectToMqtt();
        }
    } else {
        client.loop();

        unsigned long now = millis();
        if (now - lastMsg > DATA_INTERVAL) {
            lastMsg = now;
            publishSensorData();
        }
    }
}
