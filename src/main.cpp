#define TINY_GSM_MODEM_BG96      // Quectel BG96/EC200U family
#define TINY_GSM_USE_GPRS true   // Use GPRS for internet
//#define TINY_GSM_DEBUG Serial   // Uncomment to see AT commands

#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

// Pin definitions
#define MODEM_RX       16  // ESP32 RX2 ➔ Modem TX
#define MODEM_TX       17  // ESP32 TX2 ➔ Modem RX
#define MODEM_PWRKEY   19  // Modem PWRKEY control
#define STATUS_LED     2   // On-board/external LED
#define RELAY1_PIN     21  // Relay 1 control
#define RELAY2_PIN     22  // Relay 2 control

// Network credentials
const char APN[]       = "airtelgprs.com";
const char GPRS_USER[] = "";
const char GPRS_PASS[] = "";

// MQTT broker settings
const char* MQTT_BROKER_IP            = "16.171.181.237";
const uint16_t MQTT_PORT              = 1883;
const char* MQTT_USER                 = "django";
const char* MQTT_PASSWORD             = "mosquitto321";
const char* MQTT_CLIENT_ID            = "esp0_client";
const char* MQTT_SUB_TOPIC            = "cmd/esp0";
const char* MQTT_PUB_TOPIC_STATUS     = "status/esp0";
const char* MQTT_PUB_TOPIC_HEARTBEAT  = "devices/esp0heartbeat";
const char* MQTT_PUB_TOPIC_ACK        = "ack/esp0";

// OTA Update Settings
const char* MQTT_SUB_TOPIC_OTA        = "ota/esp0";
const char* GITHUB_RAW_URL = "https://raw.githubusercontent.com/KULVARSHNEY/esp32-cellular-relay/main/firmware/firmware.bin";
const char* FIRMWARE_VERSION = "1.0.0";

// Serial interface for modem AT commands
#define SerialAT Serial2

// TinyGSM and MQTT clients
TinyGsm modem(SerialAT);
TinyGsmClient gsmClient(modem);
PubSubClient mqtt(gsmClient);

// Timers
unsigned long lastReconnectAttempt = 0;
unsigned long lastHeartbeat       = 0;
unsigned long lastOTACheck        = 0;
const unsigned long HEARTBEAT_INTERVAL = 60000UL;  // 60s
const unsigned long OTA_CHECK_INTERVAL = 3600000UL; // 1 hour

// Track last relay command
String lastCommand = "";

// Forward declarations
boolean mqttConnect();
void sendHeartbeat();
void performOTAUpdate();
void checkForUpdates();

// Enhanced MQTT callback with OTA support
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  char cmd[32];
  if (len >= sizeof(cmd)) len = sizeof(cmd) - 1;
  memcpy(cmd, payload, len);
  cmd[len] = '\0';
  Serial.printf("MQTT message on [%s]: %s\n", topic, cmd);

  if (strcmp(topic, MQTT_SUB_TOPIC) == 0) {
    // Relay control logic
    if (strcmp(cmd, "relay1on") == 0) {
      lastCommand = String(cmd);
      Serial.println(F("→ relay1 ON"));
      digitalWrite(RELAY1_PIN, HIGH);
      digitalWrite(STATUS_LED, HIGH);
      delay(1000);
      digitalWrite(RELAY1_PIN, LOW);
      mqtt.publish(MQTT_PUB_TOPIC_ACK, cmd);
    }
    else if (strcmp(cmd, "relay1off") == 0) {
      lastCommand = String(cmd);
      Serial.println(F("→ relay1 OFF"));
      digitalWrite(RELAY1_PIN, LOW);
      digitalWrite(RELAY2_PIN, HIGH);
      digitalWrite(STATUS_LED, LOW);
      delay(1000);
      digitalWrite(RELAY2_PIN, LOW);
      mqtt.publish(MQTT_PUB_TOPIC_ACK, cmd);
    }
    else if (strcmp(cmd, "check_update") == 0) {
      Serial.println("Manual OTA update check triggered");
      mqtt.publish(MQTT_PUB_TOPIC_ACK, "checking_for_updates");
      checkForUpdates();
    }
    else {
      Serial.println(F("⚠ Unknown command"));
    }
  }
  else if (strcmp(topic, MQTT_SUB_TOPIC_OTA) == 0) {
    // OTA commands
    if (strcmp(cmd, "update") == 0) {
      Serial.println("OTA update triggered via MQTT");
      mqtt.publish(MQTT_PUB_TOPIC_ACK, "starting_ota_update");
      performOTAUpdate();
    }
  }
}

boolean mqttConnect() {
  Serial.print("Connecting to MQTT broker... ");
  bool status = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
  if (!status) {
    Serial.println("FAILED");
    return false;
  }
  Serial.println("OK");
  mqtt.subscribe(MQTT_SUB_TOPIC);
  mqtt.subscribe(MQTT_SUB_TOPIC_OTA); // Subscribe to OTA topic
  mqtt.publish(MQTT_PUB_TOPIC_STATUS, "connected");
  
  // Publish current firmware version
  StaticJsonDocument<128> versionDoc;
  versionDoc["version"] = FIRMWARE_VERSION;
  versionDoc["type"] = "firmware_info";
  char versionBuf[128];
  serializeJson(versionDoc, versionBuf);
  mqtt.publish(MQTT_PUB_TOPIC_STATUS, versionBuf);
  
  return true;
}

void sendHeartbeat() {
  StaticJsonDocument<256> doc;
  doc["uptime_s"] = (uint32_t)(millis() / 1000);
  doc["csq"] = modem.getSignalQuality();
  doc["gprs"] = modem.isGprsConnected();
  doc["mqtt"] = mqtt.connected();
  doc["command"] = lastCommand;
  doc["version"] = FIRMWARE_VERSION;
  doc["type"] = "heartbeat";
  
  char buf[256];
  serializeJson(doc, buf);
  mqtt.publish(MQTT_PUB_TOPIC_HEARTBEAT, buf, true);
  Serial.printf("Heartbeat: %s\n", buf);
}

void performOTAUpdate() {
  Serial.println("Starting OTA update...");
  
  HTTPClient http;
  http.begin(gsmClient, GITHUB_RAW_URL);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("Firmware size: %d bytes\n", contentLength);
    
    if (contentLength > 0) {
      bool updateSuccess = false;
      WiFiClient* stream = http.getStreamPtr();
      
      // Start update
      if (Update.begin(contentLength)) {
        size_t written = Update.writeStream(*stream);
        
        if (written == contentLength) {
          Serial.println("Written : " + String(written) + " successfully");
        } else {
          Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
        }
        
        if (Update.end()) {
          Serial.println("OTA done!");
          if (Update.isFinished()) {
            Serial.println("Update successfully completed. Rebooting...");
            updateSuccess = true;
            mqtt.publish(MQTT_PUB_TOPIC_STATUS, "update_success_rebooting");
            delay(1000);
            ESP.restart();
          } else {
            Serial.println("Update not finished? Something went wrong!");
          }
        } else {
          Serial.println("Error Occurred: " + String(Update.getError()));
        }
      } else {
        Serial.println("Not enough space to begin OTA");
      }
      
      if (!updateSuccess) {
        mqtt.publish(MQTT_PUB_TOPIC_STATUS, "update_failed");
      }
    } else {
      Serial.println("Content length is 0");
      mqtt.publish(MQTT_PUB_TOPIC_STATUS, "update_failed_no_content");
    }
  } else {
    Serial.printf("HTTP request failed, error: %s\n", http.errorToString(httpCode).c_str());
    mqtt.publish(MQTT_PUB_TOPIC_STATUS, "update_failed_http_error");
  }
  
  http.end();
}

void checkForUpdates() {
  Serial.println("Checking for firmware updates...");
  
  // You can implement version checking logic here
  // For example, check a version file on GitHub first
  String versionCheckURL = "https://raw.githubusercontent.com/yourusername/your-repo/main/version.txt";
  
  HTTPClient http;
  http.begin(gsmClient, versionCheckURL);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String latestVersion = http.getString();
    latestVersion.trim();
    
    Serial.printf("Current version: %s, Latest version: %s\n", FIRMWARE_VERSION, latestVersion.c_str());
    
    if (latestVersion != FIRMWARE_VERSION) {
      Serial.println("New version available!");
      mqtt.publish(MQTT_PUB_TOPIC_STATUS, "update_available");
      // Auto-update or wait for command
      // performOTAUpdate(); // Uncomment for auto-update
    } else {
      Serial.println("Firmware is up to date");
      mqtt.publish(MQTT_PUB_TOPIC_STATUS, "firmware_current");
    }
  } else {
    Serial.println("Failed to check version");
    mqtt.publish(MQTT_PUB_TOPIC_STATUS, "version_check_failed");
  }
  
  http.end();
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // I/O initialization
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(STATUS_LED, LOW);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  // Power on modem
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(100);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);

  // UART for AT commands
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(6000);

  // Initialize modem
  Serial.println("Initializing modem...");
  if (!modem.restart()) {
    Serial.println("Restart failed, calling init()");
    modem.init();
  }
  Serial.print("Modem Info: ");
  Serial.println(modem.getModemInfo());

  // Network registration
  Serial.println("Waiting for network...");
  if (!modem.waitForNetwork()) {
    Serial.println("Network registration failed");
  } else {
    Serial.println("Network connected");
  }

  // GPRS connection
  Serial.printf("Connecting to GPRS (APN=%s)...\n", APN);
  if (!modem.gprsConnect(APN, GPRS_USER, GPRS_PASS)) {
    Serial.println("GPRS connect failed");
  } else {
    Serial.println("GPRS connected");
  }

  // MQTT client setup
  mqtt.setServer(MQTT_BROKER_IP, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  lastReconnectAttempt = 0;
  
  Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);
}

void loop() {
  // Keep cellular and GPRS up
  if (!modem.isNetworkConnected()) {
    Serial.println("** Network disconnected, retrying...");
    if (!modem.waitForNetwork(180000L, true)) {
      Serial.println("Network reconnect failed");
      delay(10000);
      return;
    }
    Serial.println("Network reconnected");
  }
  if (!modem.isGprsConnected()) {
    Serial.println("** GPRS disconnected, retrying...");
    if (!modem.gprsConnect(APN, GPRS_USER, GPRS_PASS)) {
      Serial.println("GPRS reconnect failed");
      delay(10000);
      return;
    }
    Serial.println("GPRS reconnected");
  }

  // MQTT reconnect logic
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 10000UL) {
      lastReconnectAttempt = now;
      if (mqttConnect()) {
        lastReconnectAttempt = 0;
      }
    }
    delay(100);
    return;
  }

  // Process MQTT and heartbeat
  mqtt.loop();
  
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }
  
  // Periodic OTA check (optional)
  if (millis() - lastOTACheck >= OTA_CHECK_INTERVAL) {
    lastOTACheck = millis();
    checkForUpdates();
  }
}
