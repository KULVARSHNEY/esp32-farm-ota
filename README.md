# ESP32 Cellular Relay Controller

ESP32-based relay controller with cellular connectivity (BG96 modem) and OTA update capability.

## Features

- Cellular connectivity via Quectel BG96 modem
- MQTT communication for remote control
- OTA (Over-The-Air) firmware updates
- Relay control with status feedback
- Heartbeat monitoring
- GitHub Actions automated builds

## Hardware Requirements

- ESP32 Dev Board
- Quectel BG96 Cellular Modem
- 2-Channel Relay Module
- LEDs for status indication
- Power supply

## Pin Configuration

| ESP32 Pin | Connected To |
|-----------|--------------|
| GPIO16    | Modem RX     |
| GPIO17    | Modem TX     |
| GPIO19    | Modem PWRKEY |
| GPIO2     | Status LED   |
| GPIO21    | Relay 1      |
| GPIO22    | Relay 2      |

## MQTT Topics

- **Subscribe**: `cmd/esp0` - Receive commands
- **Publish**: `status/esp0` - Device status
- **Publish**: `ack/esp0` - Command acknowledgments
- **Publish**: `devices/esp0heartbeat` - Heartbeat messages
- **Subscribe**: `ota/esp0` - OTA update commands

## Commands

### Relay Control
- `relay1on` - Turn relay 1 on for 1 second
- `relay1off` - Turn relay 1 off for 1 second

### OTA Updates
- `check_update` - Check for new firmware version
- Send `update` to `ota/esp0` topic to trigger OTA update

## OTA Update Process

1. Update the firmware version in `src/main.cpp`
2. Create and push a git tag: `git tag v1.0.1 && git push origin v1.0.1`
3. GitHub Actions will automatically build and deploy the new firmware
4. Trigger OTA update via MQTT or wait for automatic check

## Setup Instructions

1. Clone this repository
2. Update MQTT broker details in `src/main.cpp`
3. Update GitHub RAW URL with your repository path
4. Connect hardware according to pin configuration
5. Build and upload initial firmware via USB
6. Subsequent updates can be done OTA

## Version History

- v1.0.0 - Initial release with OTA capability