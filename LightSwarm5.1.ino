#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

/*
 * LightSwarm ESP8266 Node Code with Direct LED Bar Graph Control and Smoothed Readings
 */

// Wi-Fi Network Credentials
const char* ssid = "SETUP-29FC";            // Replace with your Wi-Fi SSID
const char* password = "canyon0474charm";   // Replace with your Wi-Fi password

// Multicast settings for UDP communication
IPAddress multicastAddress(224, 1, 1, 1);    // Multicast IP for broadcasting (allowed)
unsigned int localUdpPort = 2910;            // Local port

// UDP communication setup
WiFiUDP udp;  // UDP object for communication

// Define Packet Types
#define LIGHT_UPDATE_PACKET 0  // Light sensor reading packet
#define RESET_SWARM_PACKET 1   // Reset command packet

// Packet buffer setup
const int PACKET_SIZE = 14;    // Packet size
byte packetBuffer[PACKET_SIZE]; // Buffer for packet data

// GPIO pin setup for LED control
const int onboardLedPin = LED_BUILTIN; // Onboard LED (active LOW)

// Sensor setup
const int lightSensorPin = A0;    // Light sensor pin

// Device ID setup (unique for each device)
// Set this to 0x01, 0x02, 0x03, etc., for each device
const uint8_t deviceID = 0x04;    // Example: 0x04 for this device

// State tracking variables
bool masterState = false;         // Master status
int analogValue = 0;              // Smoothed light sensor reading

// Device information structure
struct DeviceInfo {
  uint8_t deviceID;          // Device ID
  uint16_t reading;          // Sensor reading
  unsigned long lastHeard;   // Last communication time (in milliseconds)
  bool masterState;          // Master status
};

DeviceInfo devices[10];  // Device information array (supports up to 10 devices)
int numDevices = 0;      // Number of devices currently tracked

// Timing variables
unsigned long lastPacketTime = 0;      // Last packet time
unsigned long lastBroadcastTime = 0;   // Last broadcast time
unsigned long lastMasterCheckTime = 0; // Last master check time

// GPIO Pins for LED Bar Graph (Corresponding to NodeMCU pins)
const int ledPins[] = {
  D1, // GPIO5
  D2, // GPIO4
  D8, // GPIO15
  D3, // GPIO2
  D5, // GPIO14
  D6, // GPIO12
  D7, // GPIO13
  D0  // GPIO16
};

const int numLEDs = sizeof(ledPins) / sizeof(ledPins[0]);

// Define actual min and max analog values (adjust based on your observations)
const int analogMin = 150;    // Adjust if necessary
const int analogMax = 1050;   // Adjust if necessary

// Moving Average Variables
#define AVERAGE_WINDOW_SIZE 10
int analogReadings[AVERAGE_WINDOW_SIZE];
int analogReadIndex = 0;
long totalAnalogValue = 0;

// Global variable to track current master device ID
uint8_t currentMasterID = deviceID; // Initialize to self

void setup() {
  Serial.begin(115200);  // Initialize serial communication

  // Initialize the onboard LED
  pinMode(onboardLedPin, OUTPUT);
  digitalWrite(onboardLedPin, HIGH); // Turn off onboard LED (active LOW)

  // Initialize LED Bar Graph Pins
  for (int i = 0; i < numLEDs; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW); // Turn off LEDs initially
  }

  // Initialize analogReadings array
  for (int i = 0; i < AVERAGE_WINDOW_SIZE; i++) {
    analogReadings[i] = 0;
  }

  // Wi-Fi connection setup
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to Wi-Fi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Device ID: 0x%02X\n", deviceID);

  // Start listening to multicast address
  udp.beginMulticast(WiFi.localIP(), multicastAddress, localUdpPort);
  Serial.printf("Listening on Multicast IP %s, Port %d\n", multicastAddress.toString().c_str(), localUdpPort);

  // Initialize device list with self
  devices[0] = { deviceID, 0, millis(), masterState };
  numDevices = 1;
  lastPacketTime = millis();
  lastBroadcastTime = millis();
  lastMasterCheckTime = millis();
}

void loop() {
  unsigned long currentMillis = millis();

  // Read sensor value
  int newAnalogValue = analogRead(lightSensorPin);

  // Update the moving average
  totalAnalogValue = totalAnalogValue - analogReadings[analogReadIndex];
  analogReadings[analogReadIndex] = newAnalogValue;
  totalAnalogValue = totalAnalogValue + analogReadings[analogReadIndex];
  analogReadIndex = (analogReadIndex + 1) % AVERAGE_WINDOW_SIZE;

  // Calculate the average
  analogValue = totalAnalogValue / AVERAGE_WINDOW_SIZE;

  // Map analog value to LED bar graph level based on smooth transition
  int level = map(analogValue, analogMin, analogMax, 0, numLEDs);

  // Ensure level does not exceed the number of LEDs
  if (level > numLEDs) level = numLEDs;
  if (level < 0) level = 0;

  // Update LED Bar Graph based on calculated level
  updateBarGraph(level);

  // Update own device info
  devices[0].reading = analogValue; // Use normal analog value
  devices[0].lastHeard = currentMillis;
  devices[0].masterState = masterState;

  // Handle incoming packets
  handleIncomingPackets(currentMillis);

  // Broadcast readings if needed
  if (shouldBroadcast(currentMillis)) {
    broadcastReadings();
    lastBroadcastTime = currentMillis;
  }

  // Determine master status periodically (every 100 ms)
  if (currentMillis - lastMasterCheckTime >= 100) {
    determineMaster(currentMillis);
    lastMasterCheckTime = currentMillis;
  }

  // Update onboard LED based on master status
  updateOnboardLED();

  // Debugging: Print master information, analogValue, and level
  Serial.print("Current Master Device ID: 0x");
  Serial.print(currentMasterID, HEX);
  Serial.print(" | This Device is: ");
  Serial.print(masterState ? "Master" : "Slave");
  Serial.print(" | Analog Value: ");
  Serial.print(analogValue);
  Serial.print(" | LEDs Lit: ");
  Serial.println(level);

  delay(100); // Short delay to prevent flooding the serial monitor
}

void updateBarGraph(int level) {
  // Level ranges from 0 to numLEDs
  for (int i = 0; i < numLEDs; i++) {
    if (i < level) {
      digitalWrite(ledPins[i], HIGH); // Turn on LED
    } else {
      digitalWrite(ledPins[i], LOW);  // Turn off LED
    }
  }
}

void handleIncomingPackets(unsigned long currentMillis) {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    udp.read(packetBuffer, PACKET_SIZE);
    lastPacketTime = currentMillis;
    Serial.println("Packet received.");

    if (isValidPacket()) {
      processPacket(currentMillis);
    } else {
      Serial.println("Invalid packet format.");
    }
  }
}

bool isValidPacket() {
  return packetBuffer[0] == 0xF0 && packetBuffer[13] == 0x0F;
}

void processPacket(unsigned long currentMillis) {
  uint8_t packetType = packetBuffer[1];

  if (packetType == LIGHT_UPDATE_PACKET) {
    handleLightUpdatePacket(currentMillis);
  } else if (packetType == RESET_SWARM_PACKET) {
    Serial.println("RESET_SWARM_PACKET received. Resetting device.");
    resetDevice();
  }
}

void handleLightUpdatePacket(unsigned long currentMillis) {
  uint8_t senderID = packetBuffer[2];
  uint8_t senderMasterState = packetBuffer[3];
  uint16_t senderReading = (packetBuffer[5] << 8) | packetBuffer[6];

  updateDeviceList(senderID, senderReading, senderMasterState == 1, currentMillis);

  // Remove devices not heard from in over 3 seconds
  removeStaleDevices(currentMillis);

  // Debug: Print device list
  printDeviceList();
}

void updateDeviceList(uint8_t senderID, uint16_t senderReading, bool senderMasterState, unsigned long currentMillis) {
  bool found = false;
  for (int i = 0; i < numDevices; i++) {
    if (devices[i].deviceID == senderID) {
      devices[i].reading = senderReading;
      devices[i].lastHeard = currentMillis;
      devices[i].masterState = senderMasterState;
      found = true;
      break;
    }
  }

  if (!found && numDevices < 10) {
    devices[numDevices] = { senderID, senderReading, currentMillis, senderMasterState };
    numDevices++;
  }
}

void removeStaleDevices(unsigned long currentMillis) {
  for (int i = 1; i < numDevices; i++) { // Start from 1 to exclude self
    if (currentMillis - devices[i].lastHeard > 3000) {
      // Remove device by shifting the array
      for (int j = i; j < numDevices - 1; j++) {
        devices[j] = devices[j + 1];
      }
      numDevices--;
      i--; // Adjust index after removal
    }
  }
}

void printDeviceList() {
  Serial.println("Device List:");
  for (int i = 0; i < numDevices; i++) {
    Serial.printf("Device ID: 0x%02X, Reading: %d, Master: %s\n",
                  devices[i].deviceID,
                  devices[i].reading,
                  devices[i].masterState ? "Yes" : "No");
  }
}

bool shouldBroadcast(unsigned long currentMillis) {
  return (currentMillis - lastBroadcastTime >= 500) && (currentMillis - lastPacketTime >= 100);
}

void broadcastReadings() {
  memset(packetBuffer, 0, PACKET_SIZE);

  packetBuffer[0] = 0xF0;
  packetBuffer[1] = LIGHT_UPDATE_PACKET;
  packetBuffer[2] = deviceID;
  packetBuffer[3] = masterState ? 1 : 0;
  packetBuffer[4] = 1;  // Version number
  packetBuffer[5] = (devices[0].reading >> 8) & 0xFF;
  packetBuffer[6] = devices[0].reading & 0xFF;
  packetBuffer[13] = 0x0F;

  udp.beginPacketMulticast(multicastAddress, localUdpPort, WiFi.localIP());
  udp.write(packetBuffer, PACKET_SIZE);
  udp.endPacket();

  Serial.printf("Broadcasted readings. Master State: %d\n", masterState ? 1 : 0);
}

void determineMaster(unsigned long currentMillis) {
  uint16_t highestReading = devices[0].reading; // Use normal analog value
  uint8_t masterID = deviceID;

  for (int i = 0; i < numDevices; i++) {
    if (currentMillis - devices[i].lastHeard <= 3000) {
      if (devices[i].reading > highestReading ||
          (devices[i].reading == highestReading && devices[i].deviceID > masterID)) {
        highestReading = devices[i].reading;
        masterID = devices[i].deviceID;
      }
    }
  }

  // Update currentMasterID
  currentMasterID = masterID;

  if (masterID == deviceID && !masterState) {
    masterState = true;
    Serial.println("This device is now the master.");
  } else if (masterID != deviceID && masterState) {
    masterState = false;
    Serial.println("This device is no longer the master.");
  }
}

void updateOnboardLED() {
  if (masterState) {
    digitalWrite(onboardLedPin, LOW);  // Turn on onboard LED (active LOW)
  } else {
    digitalWrite(onboardLedPin, HIGH); // Turn off onboard LED
  }
}

void resetDevice() {
  // Turn off onboard LED
  digitalWrite(onboardLedPin, HIGH); // Turn off onboard LED (active LOW)

  // Turn off LED Bar Graph
  updateBarGraph(0);

  // Wait for 3 seconds
  delay(3000);

  // Reset device state
  masterState = false;
  analogValue = 0;

  // Clear device list (except self)
  numDevices = 1;
  devices[0].reading = analogValue;
  devices[0].lastHeard = millis();
  devices[0].masterState = masterState;

  // Update currentMasterID
  currentMasterID = deviceID;

  Serial.println("Device has been reset.");
} // modify this code just to send the last 3 digits of its ip ... no other part of the code should be hampered 