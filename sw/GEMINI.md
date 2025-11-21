# Toplik Service

## Project Overview

This project contains two parallel Python backend services, `ToplikServiceWIN` and `ToplikServiceRPI3`, designed to act as a central bridge for controlling "Toplik" branded ESP32 devices over a local network. The service provides a web-based interface for both guests and administrators, as well as an external API for integration with third-party systems like hotel management software.

The core technologies used are:
*   **Backend:** Python with the Flask web framework.
*   **WSGI Server:** Waitress is used for serving the application in a production environment.
*   **Authentication:** JSON Web Tokens (JWT) are used for securing the guest and admin API endpoints.
*   **Device Communication:** The server communicates with ESP32 devices via HTTP GET requests, discovering them on the network using their mDNS hostnames (e.g., `soba301.local`).

There are two main user-facing components:
1.  **Guest Portal:** A simple login page where guests enter a PIN to access a control panel for their specific room's devices (thermostat, lights, etc.).
2.  **Admin Dashboard:** A password-protected area for administrators to get a status overview of all connected devices, perform diagnostics (restart, set time), and manage system settings.

## Building and Running

The following instructions are based on the `ToplikServiceWIN` version, which is well-documented for a Windows environment. The process for the Raspberry Pi version is analogous.

### 1. Configuration

Before running the application, you must configure it by editing the `config.json` file (or `config_server.json` for the RPI version):

*   Set the `admin_password` for the admin dashboard.
*   Set the `external_api_key` for the external PIN management API.
*   Define each room (`soba`) with its unique ID, `ime` (name), `mdns` address, `port`, and the `guest_pin`.
*   Map the devices within each room, specifying their command type (`CMD`) and controller `ID`.

### 2. Installation

The project dependencies are listed in `requirements.txt`.

On Windows, you can run the provided batch script:
```bash
instaliraj_biblioteke.bat
```
Alternatively, you can manually install the packages using pip:
```bash
pip install -r requirements.txt
```

### 3. Running the Server

For testing and development, you can run the server directly from the command line.

On Windows, use the provided batch script:
```bash
pokreni_server.bat
```
This will start the server, and you should see output indicating it's running on `http://0.0.0.0:5000`.

To run it manually:
```bash
python server.py
```

### 4. Running as a Windows Service (Production)

The `README.txt` file provides detailed, step-by-step instructions for setting up the application as a persistent Windows service using the included `nssm.exe` (Non-Sucking Service Manager) tool. This ensures the server starts automatically with the operating system and restarts if it crashes.

## Development Conventions

*   **Code Style:** The Python code is procedural with functions organized by feature (configuration, helpers, guest routes, admin routes, external API). It uses basic logging for outputting status and error information.
*   **Configuration Management:** Configuration is loaded from a `config.json` file into a global `CONFIG` dictionary at startup. A `threading.Lock` is used to ensure thread-safe access to the configuration, which is important as the server runs in a multi-threaded environment (via Waitress) and has background tasks.
*   **Device Discovery:** The server relies on a background thread (`background_resolver_task`) to proactively and reactively resolve and cache the IP addresses of the ESP32 devices from their mDNS names. This prevents blocking on network lookups during an API request.
*   **API Design:** The API is split into three main parts:
    *   `/api/login`, `/api/control`, `/api/status`: Guest-facing endpoints for authentication and device control.
    *   `/api/admin/*`: Admin-only endpoints for system management.
    *   `/api/external/*`: Endpoints for third-party integration, secured by a separate API key.
*   **Error Handling:** The server provides JSON responses with a `success` flag and a `message` field to describe the outcome of an operation, which is a standard practice for web APIs.
