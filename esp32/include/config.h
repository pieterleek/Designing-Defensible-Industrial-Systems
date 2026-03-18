
#pragma once

// WiFi
const char* ssid     = "";
const char* password = "";

// MQTT Broker
const char* mqtt_server = "";
const int   mqtt_port   = 8883;

// Device Identity (moet matchen met CN in client cert)
const char* device_id  = "FlowMeter-02";
const char* fw_version = "0.1.1";

// ISA-95 Unified Namespace
const char* topic_root = "";
