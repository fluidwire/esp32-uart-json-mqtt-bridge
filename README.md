# esp32-uart-json-mqtt-bridge — forward JSON lines from an Arduino Uno over UART to MQTT and relay commands back

IoT example by [Fluidwire](https://fluidwire.com/).

Sample sketch for an [ESP32 dev board](https://fluidwire.com/) that gives an existing Arduino Uno/Nano project WiFi without rewriting it. The Uno prints one JSON object per line; the ESP32 validates each line and publishes it to MQTT. Messages sent to a command topic are written back down the UART.

## What it does

- Reads newline-terminated lines on `Serial2` (length-limited, non-blocking).
- Validates each line with ArduinoJson; bad lines are dropped and counted.
- Publishes the whole object to `<prefix>/uart/json` and each top-level key to `<prefix>/uart/<key>`.
- Relays anything published to `<prefix>/cmd` to the Uno as one text line.
- Publishes retained `online` / `offline` (Last Will) on `<prefix>/status`.

## What you need

- 1x ESP32 dev board
- 1x Arduino Uno or Nano (5V)
- 1x logic-level shifter, or a 1k + 2k resistor divider
- MQTT broker + WiFi

## Required libraries

- **ArduinoJson** by Benoit Blanchon (v7, Library Manager)
- **PubSubClient** by Nick O'Leary (Library Manager)

## Wiring

| Uno | ESP32 |
|-----|-------|
| TX | GPIO16 (RX2), **through a divider or level shifter** |
| RX | GPIO17 (TX2) |
| GND | GND |

The Uno's TX is 5V; the ESP32 is 3.3V. Use a divider (Uno TX → 1k → GPIO16, and 2k from GPIO16 to GND) or a level shifter. The ESP32 → Uno direction is safe as is.

## Setup

1. Copy `config.h.example` → `config.h`, fill in WiFi + broker.
2. On the Uno, print JSON lines at 9600 baud, e.g. `Serial.println("{\"temp\":27.5,\"hum\":61}");`
3. Flash the ESP32. Serial Monitor at **115200 baud**.
4. Watch from a PC:

```bash
mosquitto_sub -h broker.hivemq.com -t 'fluidwire/demo/bridge/#' -v
```

5. Send a command to the Uno:

```bash
mosquitto_pub -h broker.hivemq.com -t 'fluidwire/demo/bridge/cmd' -m 'LED:1'
```

## Expected serial output

```
[wifi] connecting to your-wifi-ssid
[wifi] IP: 192.168.1.50
[mqtt] connecting... ok
[uart] ok #1: {"temp":27.5,"hum":61}
[cmd] -> UART: LED:1
```

## Troubleshooting

- **Nothing arrives**: check TX/RX are crossed and the baud rates match (9600 here).
- **Garbage or dropped lines**: confirm the divider/level shifter and a common GND. "invalid JSON dropped" means the Uno sent a partial or non-JSON line.
- **"line too long"**: lines over 255 characters are dropped; shorten keys or send fewer fields.
- **Uno resets when the ESP32 boots**: don't wire the ESP32 to the Uno's reset pin; only TX/RX/GND.

## Security note

Never commit your real `config.h`. It is in `.gitignore`. The public test broker above is for demos only; use credentials and TLS for real deployments, and restrict who can publish to the `cmd` topic since it drives your hardware.

## License

MIT — see [LICENSE](LICENSE).

## Got stuck?

Drop by [fluidwire.com](https://fluidwire.com/) — happy to help.
