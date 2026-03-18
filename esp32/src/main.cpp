#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

// ============================================================
//  CONFIGURATIE
// ============================================================
#include "config.h"

// --- TOPICS ---
String topic_data;
String topic_status;
String topic_meta;

// --- NETWORK CLIENTS ---
WiFiClientSecure espClient;
PubSubClient client(espClient);

// --- TIMING ---
unsigned long lastMsg = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long DATA_INTERVAL = 5000;
const unsigned long RECONNECT_INTERVAL = 5000;

// --- WATCHDOG ---
const uint32_t WDT_TIMEOUT_SEC = 30;

// --- CERTIFICATEN ---
String ca_str, cert_str, key_str;

// ============================================================
//  WIFI & DNS
// ============================================================
void connectToWiFi()
{
    Serial.printf("[WiFi] Verbinden met '%s'... ", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // DHCP voor IP, maar we forceren onze eigen DNS server (192.168.1.1)
    IPAddress zeroIP(0, 0, 0, 0);
    WiFi.config(zeroIP, zeroIP, zeroIP);

    WiFi.begin(ssid, password);

    int retries = 0;
    const int maxRetries = 40;
    while (WiFi.status() != WL_CONNECTED && retries < maxRetries)
    {
        delay(500);
        Serial.print(".");
        retries++;
        esp_task_wdt_reset();
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf(" [OK] IP: %s | DNS: %s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.dnsIP().toString().c_str());
    }
    else
    {
        Serial.println(" [MISLUKT] Herstart in 10s...");
        delay(10000);
        ESP.restart();
    }
}

// ============================================================
//  NTP
// ============================================================
void syncTime()
{
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
    Serial.print("[NTP] Tijd syncen: ");

    int retries = 0;
    const int maxRetries = 100;
    while (time(nullptr) < 1000000000l && retries < maxRetries)
    {
        delay(100);
        retries++;
        Serial.print(".");
    }

    if (retries >= maxRetries)
    {
        Serial.println(" [TIMEOUT]");
    }
    else
    {
        time_t now = time(nullptr);
        Serial.printf(" [OK] %s", ctime(&now));
    }
}

// ============================================================
//  CERTIFICATEN LADEN (Strikte mTLS)
// ============================================================
bool loadCertificates()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("[CERT] LittleFS mount MISLUKT!");
        return false;
    }
    Serial.println("[CERT] LittleFS gemount.");

    // --- CA Certificate (Voor validatie van de server) ---
    File ca = LittleFS.open("/ca.crt", "r");
    if (!ca)
    {
        Serial.println("[CERT] FOUT: /ca.crt niet gevonden!");
        return false;
    }
    ca_str = ca.readString();
    ca.close();

    // --- Client Certificate ---
    File cert = LittleFS.open("/esp32.crt", "r");
    if (!cert)
    {
        Serial.println("[CERT] FOUT: /esp32.crt niet gevonden!");
        return false;
    }
    cert_str = cert.readString();
    cert.close();

    // --- Client Private Key (Zorg dat dit de RSA variant is!) ---
    File key = LittleFS.open("/esp32.key", "r");
    if (!key)
    {
        Serial.println("[CERT] FOUT: /esp32.key niet gevonden!");
        return false;
    }
    key_str = key.readString();
    key.close();

    // mbedTLS hack: Forceer een newline aan het einde van de bestanden
    if (!ca_str.endsWith("\n"))
        ca_str += "\n";
    if (!cert_str.endsWith("\n"))
        cert_str += "\n";
    if (!key_str.endsWith("\n"))
        key_str += "\n";

    // Configureer strikte tweezijdige TLS
    espClient.setCACert(ca_str.c_str());
    espClient.setCertificate(cert_str.c_str());
    espClient.setPrivateKey(key_str.c_str());

    Serial.println("[CERT] Strikte mTLS succesvol geconfigureerd.");
    return true;
}

// ============================================================
//  MQTT
// ============================================================
void publishBirthCertificate()
{
    // Gebruik JsonDocument voor compatibiliteit met ArduinoJson v7
    JsonDocument doc;
    doc["device"] = device_id;
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["fw_version"] = fw_version;
    doc["boot_time"] = time(nullptr);
    doc["rssi"] = WiFi.RSSI();

    char buffer[512];
    serializeJson(doc, buffer);
    client.publish(topic_meta.c_str(), buffer, true);
    Serial.printf("[MQTT] Birth certificate gepubliceerd op %s\n", topic_meta.c_str());
}

bool connectToMqtt()
{
    Serial.print("[MQTT] Verbinden (mTLS)... ");

    if (client.connect(device_id, topic_status.c_str(), 1, true, "OFFLINE"))
    {
        Serial.println("[VERBONDEN]");
        client.publish(topic_status.c_str(), "ONLINE", true);
        publishBirthCertificate();
        return true;
    }
    else
    {
        Serial.printf("[MISLUKT] rc=%d\n", client.state());
        return false;
    }
}

// ============================================================
//  SENSOR DATA
// ============================================================
void publishSensorData()
{
    float flow = random(200, 450) / 10.0;
    float temp = random(180, 220) / 10.0;

    JsonDocument doc;
    doc["timestamp"] = time(nullptr);
    doc["FlowRate"] = flow;
    doc["Temperature"] = temp;

    char buffer[256];
    size_t len = serializeJson(doc, buffer);

    if (client.beginPublish(topic_data.c_str(), len, false))
    {
        client.print(buffer);
        client.endPublish();
        Serial.printf("[TX] %s\n", buffer);
    }
    else
    {
        Serial.println("[TX] Publish MISLUKT!");
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("\n========================================");
    Serial.printf("  QmanNexus - %s v%s\n", device_id, fw_version);
    Serial.println("========================================");

    // Watchdog
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.printf("[WDT] Watchdog actief (%ds timeout)\n", WDT_TIMEOUT_SEC);

    // Random seed
    randomSeed(analogRead(0));

    // Topics instellen
    topic_data = String(topic_root) + "/" + device_id + "/DATA";
    topic_status = String(topic_root) + "/" + device_id + "/STATUS";
    topic_meta = String(topic_root) + "/" + device_id + "/META";

    // Verbinden met netwerk en tijd ophalen
    connectToWiFi();
    syncTime();

    // Certificaten inladen
    if (!loadCertificates())
    {
        Serial.println("[FATAL] Certificaten niet geladen. Herstart in 10s...");
        delay(10000);
        ESP.restart();
    }

    // MQTT instellen
    client.setServer(mqtt_server, mqtt_port);
    client.setBufferSize(1024);

    connectToMqtt();

    Serial.println("[SETUP] Initialisatie compleet.\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop()
{
    esp_task_wdt_reset();

    // 1. WiFi check
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Verbinding verloren! Reconnect...");
        WiFi.disconnect();
        WiFi.reconnect();
        delay(5000);
        esp_task_wdt_reset();

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.printf("[WiFi] Opnieuw verbonden. IP: %s\n",
                          WiFi.localIP().toString().c_str());
        }
        return;
    }

    // 2. MQTT check
    if (!client.connected())
    {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL)
        {
            lastReconnectAttempt = now;
            connectToMqtt();
        }
    }
    else
    {
        client.loop();

        // 3. Sensor data publiceren op interval
        unsigned long now = millis();
        if (now - lastMsg > DATA_INTERVAL)
        {
            lastMsg = now;
            publishSensorData();
        }
    }
}