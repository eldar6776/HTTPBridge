# GEMINI.md - Project Overview

This document provides a summary of the `HTTPBridge/fw` project to facilitate understanding and future development.

## Project Overview

This is a firmware project for an ESP32 microcontroller that acts as a bridge between an HTTP server and a serial bus (likely RS485, using the TinyFrame protocol). The device provides a web-based interface to monitor and control various hardware components and other devices connected to the serial bus.

### Core Technologies

*   **Hardware:** ESP32 (esp32dev board)
*   **Framework:** Arduino
*   **Build System:** PlatformIO
*   **Main Language:** C++
*   **Key Libraries:**
    *   `ESPAsyncWebServer`: Hosts an HTTP server for remote control and monitoring.
    *   `WiFiManager`: For easy WiFi configuration.
    *   `TinyFrame`: A custom serial communication protocol for the RS485 bus.
    *   `DallasTemperature` / `OneWire`: For reading temperature from DS18B20 sensors.
    *   `SunSet`: To calculate sunrise and sunset times for lighting control.
    *   `NTPClient`: For time synchronization.

### Architecture

The firmware implements several key functionalities:

1.  **HTTP-to-Serial Bridge:** The core function is to translate HTTP requests from a client into `TinyFrame` commands sent over the `Serial2` interface (RS485). It then waits for a response from the target device on the bus and relays it back to the HTTP client.

2.  **Web API:** An asynchronous web server exposes a comprehensive API accessible via `GET` requests with command parameters (e.g., `.../sysctrl?CMD=GET_STATUS`). This API allows for:
    *   Reading sensor data (temperature).
    *   Controlling local GPIO pins.
    *   Configuring WiFi, mDNS name, and the server port.
    *   Managing a multi-speed fan and valve for a thermostat system.
    *   Controlling an automated outdoor light based on time or sunrise/sunset.
    *   Sending commands to and receiving data from other controllers on the serial bus.
    *   Performing OTA (Over-the-Air) firmware updates.

3.  **Thermostat Control:** The device can function as a thermostat, controlling a 3-speed fan (`FAN_L`, `FAN_M`, `FAN_H`) and a water valve (`VALVE`) based on the temperature from a local sensor (`ONE_WIRE_PIN`). It supports both heating and cooling modes with configurable setpoints and thresholds.

4.  **Automated Lighting:** It controls an outdoor light (`LIGHT_PIN`) based on a user-defined schedule. The schedule can be a fixed time or dynamic, based on the calculated sunrise and sunset times for a specific location (Latitude/Longitude are hardcoded).

5.  **Persistence:** Device settings (WiFi credentials, mDNS hostname, thermostat configuration, timers) are stored in the ESP32's non-volatile memory using the `Preferences` library.

6.  **TinyFrame Protocol:** The project uses a specific configuration for the `TinyFrame` protocol:
    *   1-byte ID, 2-byte Length, 1-byte Type.
    *   CRC16 checksum for data integrity.
    *   A Start-of-Frame byte (`0x01`).
    *   Maximum payload size of 1024 bytes.

## Building and Running

This project is configured for PlatformIO.

*   **Build:**
    ```bash
    pio run
    ```
*   **Upload:**
    ```bash
    pio run --target upload
    ```
*   **Monitor:**
    ```bash
    pio device monitor --baud 115200
    ```
*   **Clean:**
    ```bash
    pio run --target clean
    ```

## Development Conventions

*   **Code Style:** The code follows a C++ style with extensive use of comments in what appears to be Bosnian/Serbian/Croatian. Function and variable names are a mix of English and abbreviations.
*   **Hardware Abstraction:** Pin definitions are managed via `#define` macros at the top of `src/main.cpp`.
*   **Communication:** Commands are defined in an `enum CommandType` in `src/main.cpp`, which maps directly to the `TinyFrame` message types and HTTP command strings.
*   **Configuration:** The `platformio.ini` file manages board settings and library dependencies. The `include/TF_Config.h` file configures the `TinyFrame` protocol parameters.
