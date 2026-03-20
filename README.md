# LED Digits - Home Assistant MQTT Light Controller

A D1 Mini ESP8266 project that controls SK6812 RGB+W LED strips with full Home Assistant integration via MQTT.

## Features

✨ **Core Features:**
- Home Assistant MQTT Discovery (auto-discovery)
- Full RGB color control (0-255 per channel)
- Brightness control (0-255)
- On/off state management
- Device availability status with Last Will Testament
- WiFi Manager for easy setup
- Over-The-Air (OTA) firmware updates
- Persistent configuration storage

🎯 **Hardware Support:**
- ESP8266 D1 Mini microcontroller
- SK6812 addressable LED strips
- RGBW emulation for color control

## Quick Start

1. **Flash Firmware:** Connect D1 Mini via USB and upload via PlatformIO
2. **WiFi Setup:** Connect to "LED-Digits-Setup" AP and configure WiFi + MQTT
3. **Home Assistant:** Device auto-discovers in Home Assistant
4. **Control:** Toggle, adjust brightness, pick colors from Home Assistant UI

**Detailed Setup:**
See [SETUP_GUIDE.md](SETUP_GUIDE.md) for complete installation and configuration instructions.

## MQTT Control

### Simple Commands
```bash
mosquitto_pub -h broker -t "homeassistant/light/led_digits_xxxxx/command" -m "ON"
mosquitto_pub -h broker -t "homeassistant/light/led_digits_xxxxx/command" -m "OFF"
```

### JSON Commands (Advanced)
```bash
# Set color and brightness
mosquitto_pub -h broker -t "homeassistant/light/led_digits_xxxxx/command" \
  -m '{"state":"ON","brightness":255,"color":{"r":255,"g":0,"b":0}}'

# Check device status
mosquitto_sub -h broker -t "homeassistant/light/led_digits_xxxxx/state"

# Check availability
mosquitto_sub -h broker -t "homeassistant/light/led_digits_xxxxx/availability"
```

## Configuration

### WiFi Manager (First Boot)
On startup, the device creates a setup portal with options for:
- WiFi network selection
- MQTT broker address
- MQTT username (optional)
- MQTT password (optional)

### MQTT Credentials
- Username and password are optional for anonymous brokers
- Settings persist across reboots
- To reset: Hold reset button ~10 seconds or uncomment `wm.resetSettings()`

## Documentation

- [SETUP_GUIDE.md](SETUP_GUIDE.md) - Complete installation and troubleshooting guide
- [platformio.ini](platformio.ini) - Project dependencies and configuration
- [src/led_gigitst.cpp](src/led_gigitst.cpp) - Main firmware code

## Architecture

```
WiFi Manager (WiFiManager 0.16.0)
    ↓
MQTT Connection (PubSubClient 2.8.0)
    ↓
Home Assistant MQTT Discovery
    ↓
SK6812 LED Control (FastLED 3.9.4)
```

## Requirements

- ESP8266 D1 Mini board
- SK6812 RGB+W LED strip
- MQTT broker (e.g., Mosquitto)
- Home Assistant with MQTT integration
- PlatformIO for building/uploading

## Building & Uploading

### Initial Upload (USB)
```bash
pio run -t upload
```

### OTA Update (WiFi)
```bash
pio run -t upload --upload-port DEVICE_IP_ADDRESS
```

Monitor output:
```bash
pio device monitor -b 115200
```

## Troubleshooting

- **Device not discovered:** Check MQTT broker is running and reachable
- **WiFi Manager not appearing:** Device already connected; reset via button/code
- **MQTT connection fails:** Verify broker address and credentials in WiFiManager

See [SETUP_GUIDE.md](SETUP_GUIDE.md) for detailed troubleshooting.

## License

See [LICENSE](LICENSE) file

## Support

For issues:
1. Check the [SETUP_GUIDE.md](SETUP_GUIDE.md) troubleshooting section
2. Review [Home Assistant MQTT documentation](https://www.home-assistant.io/integrations/mqtt/)
3. Check [PlatformIO documentation](https://docs.platformio.org/)
4. Review [Arduino ESP8266 documentation](https://arduino-esp8266.readthedocs.io/)