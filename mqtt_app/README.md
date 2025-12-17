# MQTT Subscriber Application Setup

This application subscribes to MQTT topics published by the ESP32 and displays the data (Temperature, pH, Last Feed) in a GUI dashboard.

## Prerequisites

Ensure you have Python installed. You will need to install the following libraries:

```powershell
pip install amqtt paho-mqtt
```

## How to Run

1.  **Open a Terminal** and navigate to this directory:

    ```powershell
    cd main/mqtt_app
    ```

2.  **Start the MQTT Broker** (Keep this terminal open):
    The app requires a local MQTT broker to be running.

    ```powershell
    amqtt -c broker_config.yaml
    ```

    _If you see warnings, you can ignore them as long as it says "Listener 'default' bind to 0.0.0.0:1883"._

3.  **Start the Subscriber App**:
    Open a **second terminal**, navigate to the same folder, and run:
    ```powershell
    python mqtt_subscriber.py
    ```

The dashboard should appear. Once your ESP32 is running and connected to the same network, data will appear automatically.

## Commands:

- set temp <n>
- set feed <n>
- force temp
- force feed
- force ph
