# Modbus RTU to TCP Gateway for Gas Flow Counters

A high-performance Modbus gateway system for 12 gas flow counter units, converting Modbus RTU to Modbus TCP on RP2040 with W5500 Ethernet.

## Hardware Features

- **MCU**: RP2040 @ 250MHz (dual-core)
- **Ethernet**: Wiznet W5500 controller
- **RS485**: Single port on UART0 with DE control and slew rate control
- **SD Card**: SDIO interface for data logging
- **LEDs**: 14 WS2812b LEDs (2 status + 12 channel indicators)
- **Trigger Inputs**: 12 GPIO inputs for flow counter event detection

## Firmware Features

### 1. Modbus RTU Master
- Non-blocking operation using queue-based architecture
- Trigger-based polling (reads all data on trigger event)
- Staggered periodic polling (one channel every 833ms, complete cycle in ~10 seconds)
  - Prevents queue overflow with 12 channels (queue holds 10 requests)
  - Updates temperature/pressure monitoring data
- Configurable baud rate (300-115200), parity (none/even/odd), stop bits (1 or 2), and timeout via Web UI
- Configuration changes apply immediately without system restart
- Automatic data caching with separate snapshot and current values

### 2. Modbus TCP Server
- Up to 4 simultaneous client connections
- Serves cached flow counter data
- Compatible with standard Modbus TCP clients
- Function codes 0x03 and 0x04 supported

### 3. Flow Counter Data Structure

Each flow counter stores two data sets:

**Snapshot Data** (captured on trigger events, registers 0-22):
- `volume` (float)
- `volume_normalised` (float)
- `flow` (float)
- `flow_normalised` (float)
- `temperature` (float) - snapshot at trigger time
- `pressure` (float) - snapshot at trigger time
- `timestamp` (uint32_t)
- `psu_volts` (float)
- `batt_volts` (float)
- `unit_ID` (10 bytes, char array)

**Current Data** (updated periodically, registers 30-33):
- `current_temperature` (float) - real-time monitoring
- `current_pressure` (float) - real-time monitoring

**Accessing Data**
- Connect via modbus TCP to port 502
- Slave ID should match target unit slave ID
- Function code 0x03 (read holding registers) supported
- Registers 0-22 for snapshot data (updated on flow trigger only)
- Registers 23-29 reserved (returns 0)
- Registers 30-33 for current data (updated via staggered polling, ~10 second cycle)

### 4. SD Card Logging
- Automatic CSV file creation per flow counter
- Filename derived from flow counter `unit_ID`
- Records on trigger events only
- Configurable per port
- Timestamp from flow counter (no RTC needed)

### 5. LED Status Indication
- **LED 0 (System)**: Blinks to show system OK (orange = SD/PSU warning)
- **LED 1 (RS485)**: Cyan = busy, Off = idle
- **LED 2-13 (Channels 1-12)**:
  - Off = Port not configured
  - Purple = Configured but never connected
  - Green = Data valid, communication OK
  - Red = Communication error (previously connected device offline)
  - Cyan = Modbus request pending
  - Blue = Trigger active (overrides other states while active)

### 6. Web UI
Modern dark-themed interface with:
- **Dashboard**: Real-time flow counter data (snapshot and current), system status, manual read buttons
- **Configuration**: RS485 settings (baud, parity, stop bits, timeout) and per-port flow counter config
- **Network**: IP configuration (DHCP/Static), hostname, Modbus TCP port
- **Files**: SD card file browser with system log access, view/download/delete controls
- **Modbus TCP**: Connection status and client list

## Project Structure

```
src/
├── gateway/
│   ├── flowCounterConfig.h/cpp    # Configuration management
│   └── flowCounterManager.h/cpp   # ModbusRTU polling & trigger handling
├── network/
│   ├── network.h/cpp               # Ethernet, web server, APIs
│   └── modbus_tcp.h/cpp            # Modbus TCP server
├── storage/
│   └── sdManager.h/cpp             # SD card operations
├── utils/
│   ├── logger.h/cpp                # Serial/SD logging
│   ├── statusManager.h/cpp         # LED management
│   └── terminalManager.h/cpp       # Serial terminal
├── hardware/
│   └── pins.h                      # Pin definitions
├── sys_init.h/cpp                  # System initialization
└── main.cpp                        # Entry point

web/
├── index.html                      # Web UI structure
├── style/style.css                 # Dark theme styling
└── script/script.js                # Dynamic functionality

lib/
└── modbus-rtu-master/              # Non-blocking Modbus RTU library
```

## Configuration Storage

All configuration stored in LittleFS (flash memory) as JSON:
- `/network_config.json` - Network and Modbus TCP settings
- `/gateway_config.json` - RS485 and flow counter port settings

## API Endpoints

### System
- `GET /api/system/status` - System health and status
- `GET /api/system/version` - Firmware version
- `POST /api/system/reboot` - Reboot system

### Network
- `GET /api/network` - Get network configuration
- `POST /api/network` - Update network configuration

### Gateway
- `GET /api/gateway/config` - Get gateway configuration
- `POST /api/gateway/config` - Update gateway configuration (auto-reinitializes Modbus RTU)
- `GET /api/gateway/data` - Get all flow counter data
- `POST /api/gateway/manual-read` - Trigger manual read for specific port

### Modbus TCP
- `GET /api/modbus-tcp/status` - Get Modbus TCP status
- `POST /api/modbus-tcp/config` - Update Modbus TCP configuration

### SD Card
- `GET /api/sd/list?path=/` - List directory contents (includes system log size)
- `GET /api/sd/download?path=<file>` - Download file
- `GET /api/sd/view?path=<file>` - View file contents in browser
- `DELETE /api/sd/delete?path=<file>` - Delete file

## Dual-Core Architecture

**Core 0** (Network & Coordination):
- Ethernet management
- Web server
- Modbus TCP server
- Network stack

**Core 1** (Peripherals & Data):
- SD card operations
- LED management
- ModbusRTU master
- Flow counter trigger monitoring
- Serial terminal

## Building & Flashing

```bash
# Build firmware
pio run

# Compress web files, move to /data and build filesystem image
pio run -t minify-fs

# Upload firmware
pio run -t upload

# Upload filesystem
pio run -t uploadfs
```

## Default Configuration

- **IP Mode**: DHCP
- **Fallback IP**: 192.168.1.100
- **Hostname**: flow-gateway
- **Modbus TCP Port**: 502
- **RS485 Baud**: 9600
- **RS485 Config**: 8N1 (8 data bits, no parity, 1 stop bit)
- **RS485 Timeout**: 200ms
- **All ports**: Disabled by default

## Usage

1. Power on the gateway
2. Connect Ethernet cable
3. Find IP address (check your DHCP server or connect serial monitor)
4. Open web browser to gateway IP
5. Configure RS485 settings (baud rate, parity, stop bits, timeout)
6. Configure each port (enable, slave ID, name, SD logging)
7. Connect flow counters to RS485 daisy chain
8. Connect trigger wires from flow counters to gateway trigger inputs
9. Use manual read buttons to verify flow counters are responding
10. Monitor dashboard for real-time snapshot and current temperature/pressure data

## CSV Log Format

Each flow counter gets its own CSV file named `<unit_ID>.csv`:

```csv
Timestamp,Volume,Volume_Norm,Flow,Flow_Norm,Temperature,Pressure,PSU_Volts,Batt_Volts
1698765432,123.45,115.32,45.67,42.11,22.5,101.3,24.1,12.8
1698765482,128.90,120.15,46.12,42.56,22.7,101.2,24.0,12.7
```

## Trigger Inputs

- Trigger inputs are **active LOW** (pulled high internally with INPUT_PULLUP)
- Edge-detected (not level-based) - triggers on falling edge
- When a flow counter pulls its trigger line LOW, the gateway:
  1. Detects the falling edge
  2. Queues a Modbus RTU read request for registers 0-22 (snapshot data)
  3. Updates the internal snapshot data cache
  4. Logs snapshot data to SD card if enabled
  5. Makes data available via Modbus TCP
  6. Increments trigger count for the port

## Hardware Connections

### RS485 Port
- **TX**: GPIO 0 (UART0_TX)
- **RX**: GPIO 1 (UART0_RX)
- **DE/RE**: Controlled via `PIN_RS485_DE`
- **Termination**: `PIN_RS485_TERM` set HIGH (120Ω termination active)

### Trigger Inputs
- **Port 1-12**: Connect to flow counter trigger outputs
- Each port has a dedicated GPIO input
- See `pins.h` for exact GPIO assignments

### Status LEDs
- WS2812b strip with 14 LEDs
- Data line: `PIN_LED_DAT`
- 5V power required

## Troubleshooting

### No Ethernet Connection
- Check cable connection
- Verify DHCP server is running
- Try setting static IP via web UI

### Flow Counter Not Responding
- Verify correct slave ID configured
- Check RS485 wiring (A, B, GND)
- Verify baud rate and parity match flow counter
- Check trigger input is connected and working

### SD Card Not Detected
- Verify SD card is formatted (FAT32 or exFAT)
- Check card is fully inserted
- Try a different SD card

### Web UI Not Loading
- Clear browser cache
- Try accessing via IP address directly
- Re-upload filesystem: `pio run -t uploadfs`

## Performance Notes

- **Modbus RTU**: Queue-based with one request processed at a time
- **Periodic Polling**: Every 10 seconds for temperature/pressure monitoring (registers 23-26)
- **Modbus TCP**: Can serve multiple clients simultaneously
- **Update Rate**: Dashboard refreshes every 2 seconds
- **Trigger Check**: Scanned every 10ms using edge detection
- **SD Card**: Logging is non-blocking
- **Config Changes**: RS485 settings apply immediately without restart (baud, parity, stop bits, timeout)

## Technical Details

### Memory Usage
- LittleFS: ~1MB reserved for configuration and web files
- Stack: 8KB per core
- Heap: ~200KB available for dynamic allocation

### Modbus Register Mapping
Flow counter data is mapped to Modbus registers as follows:

**Snapshot Data** (updated on trigger events):
- Registers 0-1: volume (float)
- Registers 2-3: volume_normalised (float)
- Registers 4-5: flow (float)
- Registers 6-7: flow_normalised (float)
- Registers 8-9: temperature (float) - snapshot at trigger
- Registers 10-11: pressure (float) - snapshot at trigger
- Registers 12-13: timestamp (uint32_t)
- Registers 14-15: psu_volts (float)
- Registers 16-17: batt_volts (float)
- Registers 18-22: unit_ID (10 bytes)

**Current Data** (updated every 10 seconds):
- Registers 30-31: current_temperature (float)
- Registers 32-33: current_pressure (float)

### Float Encoding
All floats are stored as IEEE 754 single-precision (32-bit) big-endian format across 2 Modbus registers.

### Serial Configuration
The RS485 interface supports Arduino SERIAL_* constants:
- **8N1** (0x413 = 1043): 8 data bits, no parity, 1 stop bit
- **8N2** (0x433 = 1075): 8 data bits, no parity, 2 stop bits
- **8E1** (0x411 = 1041): 8 data bits, even parity, 1 stop bit
- **8E2** (0x431 = 1073): 8 data bits, even parity, 2 stop bits
- **8O1** (0x412 = 1042): 8 data bits, odd parity, 1 stop bit
- **8O2** (0x432 = 1074): 8 data bits, odd parity, 2 stop bits

## License

Copyright © 2025 Bioeconomy Science Institute - Scion Group