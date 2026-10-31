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

String mqttBroker = "homeassistant.local"; // Default, will be loaded from storage
String mqttUsername = "";                  // Default empty, will be loaded from storage
String mqttPassword = "";                  // Default empty, will be loaded from storage

const char *CONFIG_FILE = "/mqtt_config.json";

WiFiClient espClient;
PubSubClient client(espClient);

// ======================== Reconnect Timing ========================
unsigned long lastMqttReconnectAttempt = 0;
const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;

unsigned long lastWifiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000;

// ======================== Device identification ========================
String deviceId;
String deviceName = "LED Digits";

String topic_command;
String topic_status;
String topic_discovery;
String topic_availability;

// ======================== LED State ========================
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
void setup()
{
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println();
  Serial.println("Starting LED Digits - Home Assistant Integration");

  // Initialize FastLED
  FastLED.addLeds(&rgbwEmu, leds, NUM_LEDS);
  FastLED.setBrightness(currentBrightness);

  setLEDColor(0, 255, 0); // Green startup indicator
  fill_solid(leds, NUM_LEDS, currentColor);
  FastLED.show();

  client.setBufferSize(1024); // Increase buffer size for larger MQTT messages

  // Initialize LittleFS BEFORE loading/saving config
  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed!");
  }
  else
  {
    Serial.println("LittleFS mounted successfully");
  }

  // Load saved MQTT configuration BEFORE WiFiManager creates parameters
  loadMQTTConfig();

  // Setup WiFiManager ONCE
  setupWiFiManager();

  // Generate unique device ID from MAC address
  deviceId = "led_digits_" + String(WiFi.macAddress());
  deviceId.replace(":", "");

  // Setup MQTT topics
  topic_command = "homeassistant/light/" + deviceId + "/command";
  topic_status = "homeassistant/light/" + deviceId + "/state";
  topic_discovery = "homeassistant/light/" + deviceId + "/config";
  topic_availability = "homeassistant/light/" + deviceId + "/availability";

  // Setup MQTT
  setupMQTT();

  // Setup OTA
  setupOTA();

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  Serial.println("Setup complete");
}

// ======================== Main Loop ========================
void loop()
{
  ArduinoOTA.handle();

  // ======================== WiFi reconnect handling ========================
  if (WiFi.status() != WL_CONNECTED)
  {
    if (client.connected())
    {
      client.disconnect();
    }

    unsigned long now = millis();

    if (now - lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS)
    {
      lastWifiReconnectAttempt = now;

      Serial.println("WiFi disconnected, attempting to reconnect...");
      WiFi.reconnect();
    }

    FastLED.show();
    delay(1);
    return;
  }

  // ======================== MQTT connection and message handling ========================
  if (!client.connected())
  {
    reconnectMQTT();
  }
  else
  {
    client.loop();
  }

  FastLED.show();
  delay(1);
}

// ======================== WiFi Manager Setup ========================
void setupWiFiManager()
{
  Serial.println("Starting WiFiManager...");

  WiFiManager wm;

  // Uncomment to reset saved credentials, useful for debugging
  // WiFi.disconnect(true);
  // WiFi.persistent(false);
  // wm.resetSettings();
  // ESP.eraseConfig();

  // Custom parameters
  WiFiManagerParameter mqttBrokerParam(
      "broker",
      "MQTT Broker",
      mqttBroker.c_str(),
      40);

  WiFiManagerParameter mqttUsernameParam(
      "username",
      "MQTT Username",
      mqttUsername.c_str(),
      40);

  WiFiManagerParameter mqttPasswordParam(
      "password",
      "MQTT Password",
      mqttPassword.c_str(),
      40);

  wm.addParameter(&mqttBrokerParam);
  wm.addParameter(&mqttUsernameParam);
  wm.addParameter(&mqttPasswordParam);

  wm.setSaveConfigCallback([]()
                           { Serial.println("Config should be saved"); });

  // Connect or start AP mode
  bool res = wm.autoConnect("LED-Digits-Setup", "12345678");

  if (!res)
  {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  Serial.println("Connected to WiFi!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Read the MQTT parameters and save if changed
  bool configChanged = false;

  const char *newBroker = mqttBrokerParam.getValue();
  if (newBroker && strlen(newBroker) > 0 && mqttBroker != String(newBroker))
  {
    mqttBroker = String(newBroker);
    Serial.print("New MQTT Broker set to: ");
    Serial.println(mqttBroker);
    configChanged = true;
  }

  const char *newUsername = mqttUsernameParam.getValue();
  if (newUsername && mqttUsername != String(newUsername))
  {
    mqttUsername = String(newUsername);
    Serial.print("New MQTT Username set to: ");
    Serial.println(mqttUsername);
    configChanged = true;
  }

  const char *newPassword = mqttPasswordParam.getValue();
  if (newPassword && mqttPassword != String(newPassword))
  {
    mqttPassword = String(newPassword);
    Serial.println("MQTT Password updated");
    configChanged = true;
  }

  if (configChanged)
  {
    saveMQTTConfig();
  }
}

// ======================== Load/Save MQTT Configuration ========================
void loadMQTTConfig()
{
  if (!LittleFS.exists(CONFIG_FILE))
  {
    Serial.println("No MQTT config file found, using defaults");
    return;
  }

  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile)
  {
    Serial.println("Failed to open MQTT config file");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error)
  {
    Serial.print("Failed to parse MQTT config: ");
    Serial.println(error.c_str());
    return;
  }

  if (doc["broker"].is<const char *>())
  {
    mqttBroker = doc["broker"].as<String>();
    Serial.print("Loaded MQTT Broker from config: ");
    Serial.println(mqttBroker);
  }

  if (doc["username"].is<const char *>())
  {
    mqttUsername = doc["username"].as<String>();
    Serial.print("Loaded MQTT Username from config: ");
    Serial.println(mqttUsername);
  }

  if (doc["password"].is<const char *>())
  {
    mqttPassword = doc["password"].as<String>();
    Serial.println("Loaded MQTT Password from config");
  }
}

void saveMQTTConfig()
{
  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile)
  {
    Serial.println("Failed to open config file for writing");
    return;
  }

  JsonDocument doc;
  doc["broker"] = mqttBroker;
  doc["username"] = mqttUsername;
  doc["password"] = mqttPassword;

  serializeJson(doc, configFile);
  configFile.close();

  Serial.println("MQTT config saved to LittleFS");
}

// ======================== MQTT Setup ========================
void setupMQTT()
{
  client.setServer(mqttBroker.c_str(), MQTT_PORT);
  client.setCallback(mqttCallback);

  Serial.println("MQTT client initialized");
  Serial.print("MQTT Broker: ");
  Serial.println(mqttBroker);
}

// ======================== MQTT Reconnection ========================
void reconnectMQTT()
{
  if (client.connected())
  {
    return;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi is not connected - skipping MQTT reconnect");
    return;
  }

  unsigned long now = millis();

  // Non-blocking reconnect interval
  if (now - lastMqttReconnectAttempt < MQTT_RECONNECT_INTERVAL_MS)
  {
    return;
  }

  lastMqttReconnectAttempt = now;

  Serial.print("Attempting MQTT connection to ");
  Serial.print(mqttBroker);
  Serial.print(" as ");
  Serial.print(deviceId);
  Serial.print("... ");

  bool connected = false;

  const bool useAuth = mqttUsername.length() > 0 || mqttPassword.length() > 0;

  if (useAuth)
  {
    connected = client.connect(
        deviceId.c_str(),
        mqttUsername.c_str(),
        mqttPassword.c_str(),
        topic_availability.c_str(),
        1,
        true,
        "offline");
  }
  else
  {
    connected = client.connect(
        deviceId.c_str(),
        topic_availability.c_str(),
        1,
        true,
        "offline");
  }

  if (connected)
  {
    Serial.println("connected");

    lastMqttReconnectAttempt = 0;

    publishAvailability();

    if (client.subscribe(topic_command.c_str()))
    {
      Serial.print("Subscribed to: ");
      Serial.println(topic_command);
    }
    else
    {
      Serial.print("Failed to subscribe to: ");
      Serial.println(topic_command);
    }

    publishStatus();
    publishDiscovery();
  }
  else
  {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" - will retry later");
  }
}

// ======================== MQTT Callback ========================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String message;
  message.reserve(length + 1);

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(" -> ");
  Serial.println(message);

  // Handle command
  if (String(topic) != topic_command)
  {
    return;
  }

  // Try to parse as JSON first
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);

  if (!error && doc.is<JsonObject>())
  {
    Serial.println("Parsed JSON payload");

    if (doc["state"].is<const char *>())
    {
      String state = doc["state"].as<String>();

      uint8_t r = currentColor.r;
      uint8_t g = currentColor.g;
      uint8_t b = currentColor.b;

      // Extract brightness if provided
      if (doc["brightness"].is<uint8_t>())
      {
        currentBrightness = doc["brightness"].as<uint8_t>();

        Serial.print("Brightness set to: ");
        Serial.println(currentBrightness);
      }

      // Extract RGB values if present
      if (doc["color"].is<JsonObject>())
      {
        JsonObject color = doc["color"];

        if (color["r"].is<uint8_t>())
        {
          r = color["r"].as<uint8_t>();
        }

        if (color["g"].is<uint8_t>())
        {
          g = color["g"].as<uint8_t>();
        }

        if (color["b"].is<uint8_t>())
        {
          b = color["b"].as<uint8_t>();
        }

        Serial.print("Color from JSON: R=");
        Serial.print(r);
        Serial.print(" G=");
        Serial.print(g);
        Serial.print(" B=");
        Serial.println(b);
      }

      if (state == "ON")
      {
        setLEDState(true, r, g, b);
        Serial.println("LED turned ON");
      }
      else if (state == "OFF")
      {
        setLEDState(false);
        Serial.println("LED turned OFF");
      }
      else
      {
        Serial.print("Unknown JSON state: ");
        Serial.println(state);
      }

      publishStatus();
    }

    return;
  }

  // Fallback to simple ON/OFF commands
  Serial.println("JSON parse failed, trying simple format");

  if (message == "ON")
  {
    setLEDState(true);
    publishStatus();
  }
  else if (message == "OFF")
  {
    setLEDState(false);
    publishStatus();
  }
  else
  {
    Serial.print("Unknown command: ");
    Serial.println(message);
  }
}

// ======================== MQTT Discovery ========================
void publishDiscovery()
{
  if (!client.connected())
  {
    Serial.println("MQTT not connected - cannot publish discovery");
    return;
  }

  JsonDocument doc;

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

  String payload;
  serializeJson(doc, payload);

  Serial.println("Publishing discovery...");
  Serial.print("Discovery topic: ");
  Serial.println(topic_discovery);
  Serial.print("Discovery payload: ");
  Serial.println(payload);

  bool result = client.publish(topic_discovery.c_str(), payload.c_str(), true);

  if (result)
  {
    Serial.println("Discovery published successfully");
  }
  else
  {
    Serial.println("Failed to publish discovery");
    Serial.print("MQTT state: ");
    Serial.println(client.state());
  }
}

// ======================== LED Control ========================
void setLEDState(bool state, uint8_t r, uint8_t g, uint8_t b)
{
  ledState = state;

  if (ledState)
  {
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
  }
  else
  {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.setBrightness(0);

    Serial.println("LED OFF");
  }

  FastLED.show();
}

void setLEDColor(uint8_t r, uint8_t g, uint8_t b)
{
  currentColor = CRGB(r, g, b);

  Serial.print("Color set to: R=");
  Serial.print(r);
  Serial.print(" G=");
  Serial.print(g);
  Serial.print(" B=");
  Serial.println(b);

  if (ledState)
  {
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.show();

    Serial.println("LED updated with new color");
  }
}

void updateLEDs()
{
  if (ledState)
  {
    fill_solid(leds, NUM_LEDS, currentColor);
    FastLED.setBrightness(currentBrightness);
  }
  else
  {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.setBrightness(0);
  }

  Serial.println("Updating LEDs with current state and color");
  FastLED.show();
}

// ======================== MQTT Status ========================
void publishStatus()
{
  if (!client.connected())
  {
    Serial.println("MQTT not connected - cannot publish status");
    return;
  }

  JsonDocument doc;

  doc["state"] = ledState ? "ON" : "OFF";
  doc["brightness"] = currentBrightness;

  if (ledState)
  {
    doc["color"]["r"] = currentColor.r;
    doc["color"]["g"] = currentColor.g;
    doc["color"]["b"] = currentColor.b;
  }

  String payload;
  serializeJson(doc, payload);

  Serial.print("Publishing status: ");
  Serial.println(payload);

  bool result = client.publish(topic_status.c_str(), payload.c_str(), true);

  if (!result)
  {
    Serial.println("Failed to publish status");
    Serial.print("MQTT state: ");
    Serial.println(client.state());
  }
}

// ======================== Publish Availability ========================
void publishAvailability()
{
  if (!client.connected())
  {
    Serial.println("MQTT not connected - cannot publish availability");
    return;
  }

  Serial.println("Publishing availability: online");

  bool result = client.publish(topic_availability.c_str(), "online", true);

  if (!result)
  {
    Serial.println("Failed to publish availability");
    Serial.print("MQTT state: ");
    Serial.println(client.state());
  }
}

// ======================== OTA Setup ========================
void setupOTA()
{
  ArduinoOTA.setHostname(deviceId.c_str());

  ArduinoOTA.onStart([]()
                     {
                       Serial.println("OTA Start");

                       setLEDColor(255, 255, 0); // Yellow during OTA
                       FastLED.show();
                       FastLED.delay(1000); });

  ArduinoOTA.onEnd([]()
                   {
                     Serial.println("\nOTA End");

                     setLEDColor(0, 255, 0); // Green on success
                     FastLED.show();
                     FastLED.delay(1000);

                     delay(5000); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
                          if (total > 0)
                          {
                            Serial.printf("Progress: %u%%\r", progress / (total / 100));
                          } });

  ArduinoOTA.onError([](ota_error_t error)
                     {
                       Serial.printf("Error[%u]: ", error);

                       if (error == OTA_AUTH_ERROR)
                       {
                         Serial.println("Auth Failed");
                       }
                       else if (error == OTA_BEGIN_ERROR)
                       {
                         Serial.println("Begin Failed");
                       }
                       else if (error == OTA_CONNECT_ERROR)
                       {
                         Serial.println("Connect Failed");
                       }
                       else if (error == OTA_RECEIVE_ERROR)
                       {
                         Serial.println("Receive Failed");
                       }
                       else if (error == OTA_END_ERROR)
                       {
                         Serial.println("End Failed");
                       }

                       setLEDColor(255, 0, 0); // Red on error
                       FastLED.show();
                       FastLED.delay(1000); });

  ArduinoOTA.begin();

  Serial.println("OTA Ready");
}
