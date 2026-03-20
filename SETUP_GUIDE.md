# LED Digits - Home Assistant Integration Setup Guide

## Overview
This project converts the D1 Mini LED strip controller into a Home Assistant-compatible smart light with WiFi Manager, MQTT support, MQTT Discovery, and OTA firmware updates.

## Features
- 🏠 **Home Assistant Integration** via MQTT with automatic discovery
- 📡 **MQTT Protocol** for reliable communication
- 🌐 **WiFi Manager** for easy WiFi setup on first startup
- 🔄 **OTA (Over-The-Air) Updates** for wireless firmware updates
- 💡 **LED Control** with on/off commands and color control
- 🔌 **SK6812 LED Strip** support 

## Prerequisites
- D1 Mini board (ESP8266)
- SK6812 LED strip or compatible
- MQTT Broker (e.g., Mosquitto, running on Home Assistant)
- Home Assistant instance
- USB cable for initial programming

## Initial Setup

### 1. Install PlatformIO
If you haven't already, install PlatformIO:
- Download and install VS Code
- Install the PlatformIO IDE extension

### 2. Flash Initial Firmware
1. Connect D1 Mini via USB to your computer
2. In VS Code with PlatformIO:
   - Press `Ctrl+Alt+U` (Upload)
   - Or use PlatformIO terminal: `pio run -t upload`

### 3. WiFi Manager Setup (First Boot)
On first startup:
1. The device creates a WiFi access point: **LED-Digits-Setup**
2. Connect your phone/computer to this AP
3. Password: `12345678`
4. A configuration portal auto-opens, or visit `192.168.4.1`
5. You'll see a form with:
   - **WiFi Network:** Select your WiFi network
   - **Password:** Enter WiFi password
   - **MQTT Broker:** Enter your MQTT broker address (default: `homeassistant.local`)
   - **MQTT Username:** Enter MQTT username (optional, leave empty if not required)
   - **MQTT Password:** Enter MQTT password (optional, leave empty if not required)
6. Click "Save" - the device will connect, save the configuration, and restart

**MQTT Broker Options:**
- `homeassistant.local` (if running on Home Assistant)
- `192.168.1.x` (if using separate Mosquitto server - use your actual IP)
- `localhost` (if on local network)

**MQTT Credentials:**
- Leave **MQTT Username** and **MQTT Password** empty if your broker doesn't require authentication
- If your broker requires authentication, enter the credentials here
- Credentials are stored securely in device config and never transmitted unencrypted over WiFi

**Configuration is saved!** The device will remember your WiFi, MQTT broker, and MQTT credentials across reboots. You can reconnect to WiFi Manager by:
- Holding down the reset button for 10 seconds (if available)
- Or by uncommenting `wm.resetSettings();` in the code and re-uploading

## Home Assistant Setup

### 1. Add MQTT Integration (if not already configured)
- Go to Settings → Devices & Services → Create Automation
- Search for "MQTT"
- The light should appear automatically as "LED Digits"

### 2. Verify Discovery
Check if the device appears in Home Assistant:
- Go to Settings → Devices & Services → MQTT
- Look for a new device called "LED Digits"

### 3. Control the Light
Once discovered, you can:
- Toggle on/off from Home Assistant UI
- Create automations
- Use voice commands (with voice assistant)
- Send MQTT commands directly

## Usage

### MQTT Topics
The device uses these MQTT topics:

- **Command Topic:** `homeassistant/light/{DEVICE_ID}/command`
  - Accept JSON: `{"state":"ON/OFF","brightness":0-255,"color":{"r":0-255,"g":0-255,"b":0-255}}`
  - Accept simple: `ON` or `OFF`

- **State Topic:** `homeassistant/light/{DEVICE_ID}/state`
  - Publishes JSON: `{"state":"ON/OFF","brightness":128,"color":{"r":255,"g":255,"b":255}}`

- **Availability Topic:** `homeassistant/light/{DEVICE_ID}/availability`
  - Publishes: `online` (on connection) or `offline` (on disconnect via Last Will Testament)
  - Home Assistant uses this to show device online/offline status

- **Discovery Topic:** `homeassistant/light/{DEVICE_ID}/config`
  - Used for Home Assistant MQTT Discovery

Where `{DEVICE_ID}` is: `led_digits_` + MAC address (without colons)

### LED Control Examples

**Via Home Assistant UI:**
- Toggle the light on/off
- Adjust brightness
- Pick colors

**Via MQTT (using MQTT client):**
```bash
# Turn on
mosquitto_pub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/command" -m '"ON"'

# Turn off
mosquitto_pub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/command" -m '"OFF"'

# Set color to red with full brightness
mosquitto_pub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/command" -m '{"state":"ON","brightness":255,"color":{"r":255,"g":0,"b":0}}'

# Set color to blue with 50% brightness
mosquitto_pub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/command" -m '{"state":"ON","brightness":128,"color":{"r":0,"g":0,"b":255}}'

# Just adjust brightness
mosquitto_pub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/command" -m '{"brightness":200}'

# Get current state
mosquitto_sub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/state"

# Check device availability
mosquitto_sub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/availability"
```

**With MQTT Credentials:**
If your MQTT broker requires authentication:
```bash
mosquitto_pub -h homeassistant.local -u username -P password -t "homeassistant/light/led_digits_xxxxx/command" -m '{"state":"ON","brightness":255}'
```

## OTA (Over-The-Air) Updates

### Prerequisites
- Device must be connected to WiFi and MQTT broker
- Arduino IDE or PlatformIO with OTA support
- Device is accessible from your network

### Update Process

#### Using PlatformIO with OTA
1. After initial USB programming, the device supports OTA
2. In PlatformIO:
   - Edit `platformio.ini` to add upload port:
     ```ini
     upload_protocol = espota
     upload_port = LED_DIGITS_IP_ADDRESS
     ```
   - Replace `LED_DIGITS_IP_ADDRESS` with your device's IP (find in Home Assistant or router)
   - Run `pio run -t upload` - it will upload via WiFi!

#### Visual Indicators During OTA
- **Yellow LED:** OTA in progress
- **Green LED:** OTA successful
- **Red LED:** OTA error

### Find Device IP Address
- Check your router's device list
- Check Home Assistant device page for the managed device
- Device prints IP on startup (Serial monitor at 115200 baud)

## Troubleshooting

### Device doesn't appear in Home Assistant
1. Check MQTT broker is running and accessible
2. Check logs: Connect via Serial at 115200 baud
3. Verify MQTT broker address in code is correct
4. Check firewall isn't blocking MQTT (port 1883)

### WiFi Manager doesn't start
1. Device appears as "LED-Digits-Setup" WiFi network
2. If not, it means it's already connected (check serial logs)
3. To force WiFi Manager reset, uncomment this line in code:
   ```cpp
   // wm.resetSettings();
   ```

### MQTT Connection Failed
1. Check MQTT broker is running (on Home Assistant: Settings → Add-ons → Mosquitto)
2. Check `MQTT_BROKER` address is correct
3. Check MQTT username and password match your broker configuration
4. Check firewall/network connectivity
5. Look at serial output for connection error codes (rc=1-5 indicate different failure reasons)

### MQTT Credentials Not Working
1. Verify username and password are correct in WiFiManager form
2. Some MQTT brokers are anonymous by default - leave username/password empty if not required
3. If using Home Assistant Mosquitto addon, create a user in the addon settings first
4. After changing credentials, reconnect to WiFiManager (reset settings) and enter new credentials

### LED Colors Look Wrong
- Adjust `W3` to `W2` in the RGBW configuration if color order is off:
  ```cpp
  W3  // Change to W2 if colors are swapped
  ```

### Serial Monitor (Debugging)
To see detailed logs:
1. In PlatformIO, click "Serial Monitor" icon
2. Ensure baud rate is **115200**
3. Watch for connection status and MQTT messages

## Customization

### Change Device Name
In `src/led_gigitst.cpp`:
```cpp
String deviceName = "LED Digits";  // Change this
```

### Change WiFi Credentials Reset
To allow WiFi reconfiguration:
```cpp
// Uncomment this line in setupWiFiManager()
wm.resetSettings();
```

### Add More LED Effects
The code can be extended with animation modes. Currently, it provides:
- Solid color control via MQTT
- Full RGB color support per MQTT commands

### RGB Color Control
Control LED colors via MQTT with full RGB support (0-255 per channel):
```json
{"state":"ON","color":{"r":255,"g":0,"b":0}}  // Red
{"state":"ON","color":{"r":0,"g":255,"b":0}}  // Green
{"state":"ON","color":{"r":0,"g":0,"b":255}}  // Blue
{"state":"ON","color":{"r":255,"g":255,"b":255}}  // White
```
Color changes apply immediately while maintaining current brightness setting.

To add effects, modify the main loop or add new MQTT commands.

### Brightness Control
The device supports full brightness control via MQTT (0-255):
- Default startup brightness: 128 (0-255). To change, modify in setup():
  ```cpp
  currentBrightness = 255;  // Adjust this value (0-255)
  ```
- Brightness can be adjusted dynamically via MQTT:
  ```json
  {"brightness": 200}
  ```
- Brightness is applied to the currently set color

## Device Availability & Last Will Testament

The device supports automatic availability detection through MQTT Last Will Testament (LWT):

- **On Connection:** Device publishes `online` to the availability topic
- **On Disconnect:** MQTT broker automatically publishes `offline` (via Last Will Testament)
- **Home Assistant Integration:** Home Assistant automatically tracks device online/offline status

This ensures Home Assistant knows the device status at all times, even if WiFi connection is lost.

## Configuration Storage

The device stores all configuration persistently using LittleFS (the device's file system):
- Configuration file: `/mqtt_config.json` on the device
- Stored settings: MQTT broker address, username, and password
- Automatically saved when you submit the WiFiManager form
- Automatically loaded on every boot

To **change the MQTT settings** after initial setup:
1. Connect to the WiFi Manager AP: hold reset button for ~10 seconds, or uncomment `wm.resetSettings();` and re-upload
2. Follow WiFi Manager setup steps again
3. Enter new MQTT broker, username, or password
4. Configuration is automatically saved

## Advanced: Manual MQTT Testing

### Using MQTT Explorer (GUI)
1. Download [MQTT Explorer](http://mqtt-explorer.com/)
2. Connect to your MQTT broker
3. Subscribe to `homeassistant/light/#` to see all messages
4. Publish commands to test

### Using mosquitto_pub/sub (CLI)
```bash
# Subscribe to all LED messages
mosquitto_sub -h homeassistant.local -t "homeassistant/light/#" -v

# Publish a command (in another terminal)
mosquitto_pub -h homeassistant.local -t "homeassistant/light/led_digits_xxxxx/command" -m "ON"
```

## LED Startup Indicators
- **Green**: Startup, waiting for WiFi
- **Green**: Ready and connected
- **Blue**: Setup complete, waiting for WiFi
- **Yellow**: OTA update in progress
- **Red**: Error or OTA failed

## File Structure
```
led-digits/
├── platformio.ini           # Project configuration with dependencies
├── src/
│   └── led_gigitst.cpp     # Main firmware code
├── SETUP_GUIDE.md          # This file
└── README.md               # Project overview
```

## Support
For issues or questions:
1. Check the Troubleshooting section above
2. Review Home Assistant MQTT documentation
3. Check PlatformIO documentation for OTA updates
4. Consult Arduino/ESP8266 documentation

## License
See LICENSE file

## Next Steps
1. Flash the firmware via USB
2. Connect to WiFi using WiFi Manager
3. Add to Home Assistant (should auto-discover)
4. Test MQTT control
5. Set up OTA for future updates
