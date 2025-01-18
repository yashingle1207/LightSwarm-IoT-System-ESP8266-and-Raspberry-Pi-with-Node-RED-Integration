LightSwarm IoT System: ESP8266 and Raspberry Pi with Node-RED Integration
Overview
LightSwarm is an innovative IoT system designed to monitor, visualize, and log ambient light intensity data using a network of ESP8266 modules and a Raspberry Pi. The system dynamically elects a master device based on light intensity readings, enabling decentralized decision-making. It integrates real-time visualizations using an 8x8 LED matrix, logs activity, and provides web-based insights through a Node-RED server.
________________________________________
Features
•	Decentralized Master Election: The ESP8266 module with the highest light intensity reading becomes the master.
•	Real-Time Visualization: Light intensity data is visualized on an 8x8 LED matrix and a Node-RED dashboard.
•	Logging: Detailed logs include device ID, IP address, light readings, and master transitions.
•	Reset Functionality: A button on the Raspberry Pi resets the system and clears logs and visualizations.
________________________________________

Schematics:![Screenshot 2025-01-18 113143](https://github.com/user-attachments/assets/9ca80778-0ac6-4237-8dba-b63a83370d95)



Tech Stack
Hardware
•	ESP8266 NodeMCU: Collects light intensity data using an LDR sensor and communicates via multicast UDP.
•	Raspberry Pi 4: Acts as the central hub for data aggregation, processing, and visualization.
•	8x8 LED Matrix (MAX7219): Displays light intensity traces.
•	Button and LED: Provides system reset functionality and feedback.

Software
•	Python: Used for Raspberry Pi scripting and system control.
•	C++/Arduino: Used for ESP8266 programming.
•	Node-RED: Web-based dashboard for real-time and historical visualizations.
•	SPI Protocol: Controls the MAX7219 LED matrix.
•	Multicast UDP: Enables communication between ESP8266 modules and the Raspberry Pi.
•	HTTP POST: Transfers data to the Node-RED server.

________________________________________
System Architecture
Components
1.	ESP8266 NodeMCU:
o	LDR Sensor (A0 pin) to measure ambient light.
o	LED Bar Graph to display light intensity locally.
o	Onboard LED to indicate master status.
2.	Raspberry Pi 4:
o	Button (GPIO 17) for system reset.
o	Yellow LED (GPIO 18) for visual feedback.
o	MAX7219 LED Matrix (SPI) for data visualization.
o	Node-RED server for web-based insights.

Communication Protocols
•	ESP8266 to Raspberry Pi:
o	Protocol: Multicast UDP
o	Message: Device ID, Master Status, Light Reading

![image](https://github.com/user-attachments/assets/78fb4af9-0eab-4b70-8ca3-90732465bfab)



•	Raspberry Pi to ESP8266:
o	Protocol: Multicast UDP
o	Message: Reset Command

•	Raspberry Pi to Node-RED:
o	Protocol: HTTP POST
o	Message: JSON-formatted data (average readings, scaled values, master durations).
________________________________________
Workflow
1. Initialization
•	ESP8266:
o	Connects to Wi-Fi.
o	Configures onboard peripherals (LEDs, LDR sensor).
o	Joins the multicast group.

•	Raspberry Pi:
o	Configures GPIO pins, SPI for MAX7219.
o	Sets up UDP multicast listener and Node-RED server.

2. Data Collection
•	ESP8266 reads light intensity via the LDR sensor.
•	The reading is smoothed using a moving average.
•	Sensor data is broadcasted to the multicast group.

3. Master Election
•	Each ESP8266 compares its reading with received packets.
•	The highest reading determines the master.
•	Master status is indicated by the onboard LED (active LOW).

4. Data Visualization
•	Raspberry Pi:
o	Receives and processes multicast packets.
o	Displays data on the LED matrix.
o	Sends JSON data to the Node-RED dashboard.
•	Node-RED:
o	Line Graph: Displays light intensity over time.
o	Bar Chart: Shows master device durations.

5. Logging
•	Timestamps, device IDs, readings, and master transitions are logged to a text file.

6. System Reset
•	Pressing the button on the Raspberry Pi:
o	Clears logs and visualizations.
o	Sends a reset command to all ESP8266 modules.
________________________________________
Implementation Details
1. ESP8266 Code
•	Core Functionality:
o	Reads sensor values and smooths them using a moving average.
o	Broadcasts readings and master status via UDP.
o	Handles incoming reset commands.
•	LED Bar Graph:
o	Maps light readings to LED levels.
•	Master Election:
o	Dynamically determines the master device based on the highest reading.

2. Raspberry Pi Code
•	Data Handling:
o	Listens to UDP packets and decodes light readings.
o	Updates the LED matrix with scaled data.
•	Node-RED Integration:
o	Sends data via HTTP POST.
•	Logging:
o	Logs all activity, including master transitions and light readings.
•	System Reset:
o	Clears logs, resets ESP8266 nodes, and updates Node-RED.

3. Node-RED Dashboard
•	Setup:
o	Receives JSON data from the Raspberry Pi.
o	Configures line and bar charts.
•	Visualizations:
o	Real-time light intensity traces.
o	Historical master activity durations.
________________________________________
Future Enhancements
  •	Add Temperature/Humidity Sensors: Expand functionality to include environmental data.
  •	Machine Learning: Predict master transitions based on historical data.
  •	Cloud Integration: Upload logs and visualizations to cloud platforms for remote access.
________________________________________
How to Run the Project
Hardware Setup
1.	ESP8266 Nodes:
o	Connect the LDR sensor to A0.
o	Connect the LED bar graph to GPIO pins.

2.	Raspberry Pi:
o	Connect the button and yellow LED to GPIO 17 and GPIO 18, respectively.
o	Connect the MAX7219 LED matrix via SPI.

Software Setup
1.	ESP8266:
o	Install the Arduino IDE.
o	Upload the provided ESP8266 code.

2.	Raspberry Pi:
o	Install Python dependencies:
pip install spidev gpiozero requests
o	Run the Python script.

3.	Node-RED:
o	Start the Node-RED server.
o	Import the provided flow configuration.
________________________________________
Example Log Output
2024-12-06 22:31:38 - Master Data: Device ID: 0x01, IP: 192.168.0.8, Reading: 604
2024-12-06 22:31:38 - Interval average reading: 687.16, Scaled: 4
2024-12-06 22:31:38 - Device ID: 0x01 was master for 0.34 seconds.
2024-12-06 22:31:38 - Device ID: 0x03 is now the master.
2024-12-06 22:31:38 - Master Data: Device ID: 0x03, IP: 192.168.0.69, Reading: 678
________________________________________

Conclusion

The LightSwarm IoT system demonstrates a scalable, decentralized approach to IoT data collection 
and visualization. By leveraging ESP8266 and Raspberry Pi, the system achieves real-time insights
and dynamic master election, making it ideal for smart home and industrial IoT applications.
