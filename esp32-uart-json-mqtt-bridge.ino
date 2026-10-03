/*
 * ESP32 UART JSON-to-MQTT Bridge
 * -------------------------------
 * Gives an existing Arduino Uno/Nano project WiFi without rewriting it.
 * The Uno prints one JSON object per line, e.g.  {"temp":27.5,"hum":61}
 * The ESP32 reads those lines on Serial2, validates them, and publishes:
 *   <prefix>/uart/json      the whole validated line
 *   <prefix>/uart/<key>     one topic per top-level key (value as text)
 * Anything published to <prefix>/cmd is written back down the UART as one
 * line, so MQTT can control the Uno (e.g. "LED:1").
 *
 * WIRING (ESP32 is 3.3V, Uno is 5V - mind the levels!)
 *   Uno TX  -> [voltage divider 1k/2k or level shifter] -> ESP32 GPIO16 (RX2)
 *   ESP32 GPIO17 (TX2) -> Uno RX   (3.3V into a 5V input is fine)
 *   GND <-> GND
 * Never connect the Uno's 5V TX directly to an ESP32 pin.
 *
 * Required libraries (install via Library Manager):
 *   - "ArduinoJson" by Benoit Blanchon (v7)
 *   - "PubSubClient" by Nick O'Leary
 *
 * Setup:
 *   1. Copy config.h.example to config.h, fill in WiFi + broker.
 *   2. Flash. Open Serial Monitor at 115200.
 *
 * (c) 2026 Fluidwire. Released under the MIT License.
 * https://github.com/fluidwire
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

const int      UART_RX_PIN   = 16;
const int      UART_TX_PIN   = 17;
const uint32_t UART_BAUD     = 9600;
const size_t   MAX_LINE_LEN  = 255;     // longer lines are dropped, not truncated
const size_t   MAX_CMD_LEN   = 120;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String topicJson   = String(MQTT_TOPIC_PREFIX) + "/uart/json";
String topicKeyPfx = String(MQTT_TOPIC_PREFIX) + "/uart/";
String topicCmd    = String(MQTT_TOPIC_PREFIX) + "/cmd";
String topicStatus = String(MQTT_TOPIC_PREFIX) + "/status";

char   lineBuf[MAX_LINE_LEN + 1];
size_t lineLen   = 0;
bool   overflow  = false;
uint32_t okCount = 0, badCount = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print(F("[wifi] connecting to "));
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("[wifi] IP: "));
    Serial.println(WiFi.localIP());
  }
}

// MQTT -> UART: relay a command line down to the Arduino.
void onMessage(char* topic, byte* payload, unsigned int length) {
  if (length == 0 || length > MAX_CMD_LEN) {
    Serial.println(F("[cmd] rejected (empty or too long)"));
    return;
  }
  Serial2.write(payload, length);
  Serial2.write('\n');
  Serial.print(F("[cmd] -> UART: "));
  Serial.write(payload, length);
  Serial.println();
}

void connectMQTT() {
  if (mqtt.connected()) return;
  Serial.print(F("[mqtt] connecting... "));
  bool ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                         topicStatus.c_str(), 1, true, "offline");
  if (ok) {
    Serial.println(F("ok"));
    mqtt.publish(topicStatus.c_str(), "online", true);
    mqtt.subscribe(topicCmd.c_str());
  } else {
    Serial.print(F("failed, rc="));
    Serial.println(mqtt.state());
  }
}

// Validate one line and fan it out to MQTT.
void handleLine(const char* line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err || !doc.is<JsonObject>()) {
    badCount++;
    Serial.print(F("[uart] invalid JSON dropped: "));
    Serial.println(err ? err.c_str() : "not an object");
    return;
  }
  okCount++;
  mqtt.publish(topicJson.c_str(), line);

  for (JsonPair kv : doc.as<JsonObject>()) {
    String value;
    if (kv.value().is<const char*>()) {
      value = kv.value().as<const char*>();
    } else {
      serializeJson(kv.value(), value);   // numbers, bools, nested -> text
    }
    String t = topicKeyPfx + kv.key().c_str();
    mqtt.publish(t.c_str(), value.c_str());
  }
  Serial.print(F("[uart] ok #"));
  Serial.print(okCount);
  Serial.print(F(": "));
  Serial.println(line);
}

// Non-blocking, length-limited line reader.
void pollUart() {
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (overflow) {
        badCount++;
        Serial.println(F("[uart] line too long, dropped"));
      } else if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        handleLine(lineBuf);
      }
      lineLen = 0;
      overflow = false;
      continue;
    }
    if (lineLen < MAX_LINE_LEN) lineBuf[lineLen++] = c;
    else overflow = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setCallback(onMessage);
  connectWiFi();
}

void loop() {
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    static uint32_t lastTry = 0;
    if (!mqtt.connected() && millis() - lastTry > 5000) {
      lastTry = millis();
      connectMQTT();
    }
    mqtt.loop();
  }
  pollUart();
}
