#include <Arduino.h>
#include <FastLED.h>
#include <WiFiManager.h>

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>


// ======================== LED Configuration ========================
#define DATA_PIN D0
#define LED_TYPE SK6812
#define COLOR_ORDER RGB
#define NUM_LEDS 40

CRGB leds[NUM_LEDS];

// Set up the RGBW emulation
Rgbw rgbw = Rgbw(kRGBWDefaultColorTemp, kRGBWExactColors, W3);

using ControllerT = SK6812<DATA_PIN, RGB>;
static RGBWEmulatedController<ControllerT, GRB> rgbwEmu{rgbw};

// ======================== MQTT Configuration ========================
const int MQTT_PORT = 1883;
String mqttBroker =
    "homeassistant.local"; // Default, will be loaded from storage
String mqttUsername = "";  // Default empty, will be loaded from storage
String mqttPassword = "";  // Default empty, will be loaded from storage
const char *CONFIG_FILE = "/mqtt_config.json";

WiFiClient espClient;
PubSubClient client(espClient);

// Device identification
String deviceId;
String deviceName = "LED Digits";
String topic_command;
String topic_status;
String topic_discovery;
String topic_availability;

bool ledState = false;
CRGB currentColor = CRGB::Black;

uint8_t currentBrightness = 128;

// ======================== Forward Declarations ========================
void setupWiFiManager();
void setupMQTT();
void setupOTA();
void loadMQTTConfig();
void saveMQTTConfig();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void publishDiscovery();
void publishStatus();
void publishAvailability();
void updateLEDs();
void setLEDState(bool state, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
void setLEDColor(uint8_t r, uint8_t g, uint8_t b);
void reconnectMQTT();

// ======================== Setup ========================
void setup() {

    // Initialize FastLED
  FastLED.addLeds(&rgbwEmu, leds, NUM_LEDS);
  FastLED.setBrightness(currentBrightness);

  setLEDColor(0, 255, 0); // Green startup indicator
  fill_solid(leds, NUM_LEDS, currentColor);

  client.setBufferSize(1024); // Increase buffer size for larger MQTT messages
  setupWiFiManager();
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n\nStarting LED Digits - Home Assistant Integration");

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed!");
  } else {
    Serial.println("LittleFS mounted successfully");
  }

  // Load saved MQTT configuration
  loadMQTTConfig();

  FastLED.show();
  FastLED.delay(1000);

  // Generate unique device ID from MAC address
  deviceId = "led_digits_" + String(WiFi.macAddress());
  deviceId.replace(":", "");

  // Setup MQTT topics
  topic_command = "homeassistant/light/" + deviceId + "/command";
  topic_status = "homeassistant/light/" + deviceId + "/state";
  topic_discovery = "homeassistant/light/" + deviceId + "/config";
  topic_availability = "homeassistant/light/" + deviceId + "/availability";

  // Setup WiFiManager
  setupWiFiManager();

  // Setup MQTT
  setupMQTT();

  // Setup OTA
  setupOTA();

  // delay(1000);

  // Publish discovery to Home Assistant
  // publishDiscovery();

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// ======================== Main Loop ========================
void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    WiFi.reconnect();
    delay(5000);
  }

  // MQTT connection and message handling
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();

  // OTA handling
  ArduinoOTA.handle();

  FastLED.show();
}

// ======================== WiFi Manager Setup ========================
void setupWiFiManager() {
  Serial.println("Starting WiFiManager...");

  WiFiManager wm;

  // Uncomment to reset saved credentials (useful for debugging)
  //WiFi.disconnect(true); 
  //WiFi.persistent(false);
  //wm.resetSettings();
  //ESP.eraseConfig();
  

  // Custom parameters
  WiFiManagerParameter mqttBrokerParam("broker", "MQTT Broker",
                                       mqttBroker.c_str(), 40);
  WiFiManagerParameter mqttUsernameParam("username", "MQTT Username",
                                         mqttUsername.c_str(), 40);
  WiFiManagerParameter mqttPasswordParam("password", "MQTT Password",
                                         mqttPassword.c_str(), 40);
  wm.addParameter(&mqttBrokerParam);
  wm.addParameter(&mqttUsernameParam);
  wm.addParameter(&mqttPasswordParam);

  // Save config after connection
  wm.setSaveConfigCallback([]() { Serial.println("Config should be saved"); });

  // Connect or start AP mode
  bool res = wm.autoConnect("LED-Digits-Setup", "12345678");

  if (!res) {
    Serial.println("Failed to connect and hit timeout");
    // Will restart and try again
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("Connected to WiFi!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Read the MQTT parameters and save if changed
    bool configChanged = false;

    const char *newBroker = mqttBrokerParam.getValue();
    if (newBroker && strlen(newBroker) > 0 && mqttBroker != String(newBroker)) {
      mqttBroker = String(newBroker);
      Serial.print("New MQTT Broker set to: ");
      Serial.println(mqttBroker);
      configChanged = true;
    }

    const char *newUsername = mqttUsernameParam.getValue();
    if (newUsername && mqttUsername != String(newUsername)) {
      mqttUsername = String(newUsername);
      Serial.print("New MQTT Username set to: ");
      Serial.println(mqttUsername);
      configChanged = true;
    }

    const char *newPassword = mqttPasswordParam.getValue();
    if (newPassword && mqttPassword != String(newPassword)) {
      mqttPassword = String(newPassword);
      Serial.println("MQTT Password updated");
      configChanged = true;
    }

    if (configChanged) {
      saveMQTTConfig();
    }
  }
}

// ======================== Load/Save MQTT Configuration
// ========================
void loadMQTTConfig() {
  if (LittleFS.exists(CONFIG_FILE)) {
    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (configFile) {
      String content = configFile.readString();
      configFile.close();

      // Parse broker
      int brokerStart = content.indexOf("\"broker\":\"");
      if (brokerStart != -1) {
        brokerStart += 10;
        int brokerEnd = content.indexOf("\"", brokerStart);
        if (brokerEnd != -1) {
          mqttBroker = content.substring(brokerStart, brokerEnd);
          Serial.print("Loaded MQTT Broker from config: ");
          Serial.println(mqttBroker);
        }
      }

      // Parse username
      int usernameStart = content.indexOf("\"username\":\"");
      if (usernameStart != -1) {
        usernameStart += 12; // length of "\"username\":\""
        int usernameEnd = content.indexOf("\"", usernameStart);
        if (usernameEnd != -1) {
          mqttUsername = content.substring(usernameStart, usernameEnd);
          Serial.print("Loaded MQTT Username from config: ");
          Serial.println(mqttUsername);
        }
      }

      // Parse password
      int passwordStart = content.indexOf("\"password\":\"");
      if (passwordStart != -1) {
        passwordStart += 12; // length of "\"password\":\""
        int passwordEnd = content.indexOf("\"", passwordStart);
        if (passwordEnd != -1) {
          mqttPassword = content.substring(passwordStart, passwordEnd);
          Serial.println("Loaded MQTT Password from config");
        }
      }
    }
  } else {
    Serial.println("No MQTT config file found, using defaults");
  }
}

void saveMQTTConfig() {
  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (configFile) {
    String jsonConfig = "{\"broker\":\"" + mqttBroker + "\",\"username\":\"" +
                        mqttUsername + "\",\"password\":\"" + mqttPassword +
                        "\"}";
    configFile.print(jsonConfig);
    configFile.close();
    Serial.println("MQTT config saved to LittleFS");
  } else {
    Serial.println("Failed to open config file for writing");
  }
}

// ======================== MQTT Setup ========================
void setupMQTT() {
  client.setServer(mqttBroker.c_str(), MQTT_PORT);
  client.setCallback(mqttCallback);
  Serial.println("MQTT client initialized");
  Serial.print("MQTT Broker: ");
  Serial.println(mqttBroker);
}

// ======================== MQTT Reconnection ========================
void reconnectMQTT() {
  int retries = 0;
  const int MAX_RETRIES = 3;

  while (!client.connected() && retries < MAX_RETRIES) {
    Serial.print("Attempting MQTT connection...");

    // Use username and password if set, otherwise use nullptr
    const char *username =
        (mqttUsername.length() > 0) ? mqttUsername.c_str() : nullptr;
    const char *password =
        (mqttPassword.length() > 0) ? mqttPassword.c_str() : nullptr;

    if (client.connect(deviceId.c_str(), username, password,
                       topic_availability.c_str(), 1, true, "offline")) {
      Serial.println("connected");

      // Publish availability as online
      publishAvailability();

      // Subscribe to command topic
      client.subscribe(topic_command.c_str());

      // Publish current status
      publishStatus();

      // Publish discovery again on reconnect
      publishDiscovery();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
      retries++;
    }
  }
}

// ======================== MQTT Callback ========================
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // Convert payload to string
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(message);

  // Handle command
  if (String(topic) == topic_command) {
    // Try to parse as JSON first
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (!error && doc.is<JsonObject>()) {
      // JSON payload received
      Serial.println("Parsed JSON payload");

      if (doc["state"].is<const char *>()) {
        String state = doc["state"].as<String>();
        uint8_t r = currentColor.r;
        uint8_t g = currentColor.g;
        uint8_t b = currentColor.b;

        // Extract brightness if provided
        if (doc["brightness"].is<uint8_t>()) {
          uint8_t brightness = doc["brightness"].as<uint8_t>();
          currentBrightness = brightness;
          Serial.print("Brightness set to: ");
          Serial.println(brightness);
        }

        // Extract RGB values if present
        if (doc["color"].is<JsonObject>()) {
          JsonObject color = doc["color"];
          if (color["r"].is<uint8_t>())
            r = color["r"].as<uint8_t>();
          if (color["g"].is<uint8_t>())
            g = color["g"].as<uint8_t>();
          if (color["b"].is<uint8_t>())
            b = color["b"].as<uint8_t>();
          Serial.print("Color from JSON: R=");
          Serial.print(r);
          Serial.print(" G=");
          Serial.print(g);
          Serial.print(" B=");
          Serial.println(b);
        }

        if (state == "ON") {
          setLEDState(true, r, g, b);
          Serial.println("LED turned ON");
        } else if (state == "OFF") {
          setLEDState(false);
          Serial.println("LED turned OFF");
        }

        // Publish status back
        publishStatus();
      }
    } else {
      // Fallback to simple ON/OFF commands
      Serial.println("JSON parse failed, trying simple format");
      if (String(message) == "ON") {
        setLEDState(true);
        publishStatus();
      } else if (String(message) == "OFF") {
        setLEDState(false);
        publishStatus();
      } else {
        Serial.println("Unknown command: " + String(message));
      }
    }
  }
}

// ======================== MQTT Discovery ========================
void publishDiscovery() {
  if (!client.connected()) {
    Serial.println("MQTT not connected - cannot publish discovery");
    return;
  }

  // Build discovery payload using JSON
  JsonDocument doc;

  // Base topic shorthand
  String baseTopic = topic_status.substring(0, topic_status.lastIndexOf("/"));
  doc["~"] = baseTopic;
  doc["name"] = deviceName;
  doc["uniq_id"] = deviceId;
  doc["schema"] = "json";
  doc["stat_t"] = "~/state";
  doc["cmd_t"] = "~/command";
  doc["avty_t"] = "~/availability";
  doc["pl_avail"] = "online";
  doc["pl_not_avail"] = "offline";
  doc["pl_on"] = "ON";
  doc["pl_off"] = "OFF";
  doc["brightness"] = true;
  doc["color_mode"] = true;
  doc["retain"] = true;
  doc["sup_clrm"][0] = "rgb";

  // Device mapping
  JsonObject device = doc["dev"].to<JsonObject>();
  device["ids"] = deviceId;
  device["name"] = deviceName;
  device["mdl"] = "D1 Mini SK6812";
  device["mf"] = "ESP8266";
  device["hw"] = "D1 Mini";
  device["sw"] = "1.0.0";

  // Origin mapping
  JsonObject origin = doc["o"].to<JsonObject>();
  origin["name"] = "LED Digits Firmware";
  origin["sw"] = "1.0.0";
  origin["url"] = "https://github.com/zefir-o/led-digits";

  // Serialize to string
  String payload;
  serializeJson(doc, payload);

  Serial.println("Publishing discovery...");
  Serial.print("Discovery topic: ");
  Serial.println(topic_discovery);
  Serial.print("Discovery payload: ");
  Serial.println(payload);

  if (!client.connected()) {
    Serial.println("MQTT not connected - cannot publish discovery");
  }
  bool result = client.publish(topic_discovery.c_str(), payload.c_str(), false);

  if (result) {
    Serial.println("✓ Discovery published successfully");
  } else {
    Serial.println("✗ Failed to publish discovery");
    Serial.print("MQTT state: ");
    Serial.println(client.state());
  }
}

// ======================== LED Control ========================
void setLEDState(bool state, uint8_t r, uint8_t g, uint8_t b) {
  ledState = state;

  if (ledState) {
    // Turn ON with specified color
    currentColor = CRGB(r, g, b);
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.setBrightness(currentBrightness);
    Serial.print("LED ON - R=");
    Serial.print(r);
    Serial.print(" G=");
    Serial.print(g);
    Serial.print(" B=");
    Serial.print(b);
    Serial.print(" | Brightness=");
    Serial.println(currentBrightness);
  } else {
    // Turn OFF
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.setBrightness(0);
    Serial.println("LED OFF");
  }

  FastLED.show();
}

void publishStatus() {
  // Publish current state as JSON
  JsonDocument doc;
  doc["state"] = ledState ? "ON" : "OFF";
  doc["brightness"] = currentBrightness;

  if (ledState) {
    doc["color"]["r"] = currentColor.r;
    doc["color"]["g"] = currentColor.g;
    doc["color"]["b"] = currentColor.b;
  }

  String payload;
  serializeJson(doc, payload);

  Serial.print("Publishing status: ");
  Serial.println(payload);

  client.publish(topic_status.c_str(), payload.c_str(), true);
}

// ======================== Publish Availability ========================
void publishAvailability() {
  if (!client.connected()) {
    Serial.println("MQTT not connected - cannot publish availability");
    return;
  }

  Serial.println("Publishing availability: online");
  client.publish(topic_availability.c_str(), "online", true);
}

void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  currentColor = CRGB(r, g, b);

  Serial.print("Color set to: R=");
  Serial.print(r);
  Serial.print(" G=");
  Serial.print(g);
  Serial.print(" B=");
  Serial.println(b);

  if (ledState) {
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.show();
    Serial.println("LED updated with new color");
  }
}

void updateLEDs() {
  if (ledState) {
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.setBrightness(currentBrightness);
  } else {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
  }
  Serial.println("Updating LEDs with current state and color");
  FastLED.show();
}

// ======================== OTA Setup ========================
void setupOTA() {
  ArduinoOTA.setHostname(deviceId.c_str());

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    setLEDColor(255, 255, 0); // Yellow during OTA
    FastLED.show();
    FastLED.delay(1000);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
    setLEDColor(0, 255, 0); // Green on success
    FastLED.show();
    FastLED.delay(1000);
    delay(5000);

  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");

    setLEDColor(255, 0, 0); // Red on error
    FastLED.show();
    FastLED.delay(1000);
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}