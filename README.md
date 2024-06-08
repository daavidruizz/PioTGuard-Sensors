# PioTGuard-Sensors

## Description

PioTGuard-Sensors is a program developed for the management and monitoring of security sensors using an ESP32 microcontroller. The system supports connection to a WiFi network and communication via MQTT for the transmission of sensor data and remote configuration. The monitored sensors include a door sensor, a presence sensor, and a gas sensor.

## Author

David Ruiz Luque  
University of Malaga  
Email: daavidruiz01@outlook.com  
Date: 2024-06-05

## Requirements

- ESP32
- FreeRTOS
- MQTT Broker
- SSL Certificates for MQTT
- `cJSON` library for JSON handling

## Project Structure

### Main Files

- `main.c`: Main file of the project with the implementation of functions and tasks.
- `nvs_write.h` and `nvs_write.c`: NVS (Non-Volatile Storage) handling for storing configurations.
- `defines.h`: Definitions of constants and macros used in the project.

## Features

### Sensors

The program monitors the following sensors:
- **Door Sensor**: Detects door opening and closing.
- **Presence Sensor**: Detects movement in a specific area.
- **Gas Sensor**: Measures the concentration of gas in the environment.

### Communication

- **WiFi**: Connection to a configured WiFi network stored in NVS.
- **MQTT**: Communication with an MQTT broker for data publishing and configuration reception.

### Functions

- **JSON Initialization**: Functions to initialize and handle JSON objects (`jsonSrtInit`, `jsonSrtSettings`, `jsonStrLog`).
- **Date and Time**: Configuration of date and time using NTP servers (`getDateTimeString`, `DateTime`).
- **Memory Management**: Function to print the free memory usage (`print_memory_usage`).
- **MQTT Topic Handling**: Functions to handle topics and received data (`topicToID`, `topicHandler`).

## Configuration

### WiFi Configuration

WiFi credentials are stored in the ESP32's NVS and read at the program's start.

### MQTT Configuration

The program requires an MQTT broker configuration and uses SSL certificates for a secure connection. The MQTT client configuration is done in the `mqttTask` function.

## Usage

### Compilation and Upload

1. Clone the project repository.
2. Set up the development environment for ESP32.
3. Compile and upload the program to the ESP32.

### Execution

Upon starting the program:
1. The ESP32 connects to the WiFi network.
2. The date and time are set using NTP servers.
3. Sensor monitoring tasks are initialized and run.
4. The connection to the MQTT broker is established, managing publications and subscriptions.

## System Tasks

- **Sensor Tasks**: Tasks for monitoring each sensor (door, presence, gas).
- **MQTT Task**: Task for handling MQTT communication (data publishing and configuration reception).

## Contact

For any inquiries or collaborations, contact David Ruiz Luque via email at daavidruiz01@outlook.com.
