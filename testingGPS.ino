#include <TinyGPS++.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// GPS pins
#define RXD2 14
#define TXD2 12
#define GPS_BAUD 9600

// MPU6050 I2C pins
#define SDA_PIN 21
#define SCL_PIN 22

// Since SIM800L is integrated with ESP32, we'll use serial AT commands directly
// This assumes your board has the SIM800L module connected to specific pins
// Many integrated modules use hardware Serial1 or Serial2
#define SIM_SERIAL Serial1

// MPU6050 accelerometer threshold for movement detection
#define MOVEMENT_THRESHOLD 0.5
#define MOVEMENT_TIME_THRESHOLD 5000 // 30 seconds in milliseconds

// Wi-Fi credentials
const char* ssid = "Nikola_Deco";
const char* password = "nikola2021";
const char* serverURL = "http://192.168.69.111:3000/api/data";

// Phone number to send alerts to (include country code)
const char* alertPhoneNumber = "+359879583852"; // Change to your number

// Hardware connections
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
Adafruit_MPU6050 mpu;

// Movement tracking variables
bool isMoving = false;
unsigned long movementStartTime = 0;
bool alertSent = false;
float lastX, lastY, lastZ;
unsigned long lastSampleTime = 0;
const int sampleInterval = 100; // Check accelerometer every 100ms

void setup() {
  Serial.begin(115200);
  
  // Initialize GPS
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  Serial.println("GPS module started");
  
  // Initialize integrated SIM800L module
  SIM_SERIAL.begin(115200); // Most integrated modules use 115200 baud
  delay(3000); // Give SIM800L time to initialize
  Serial.println("SIM800L module started");
  setupSIM800L();
  
  // Initialize MPU6050 with custom I2C pins
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 accelerometer started");
  
  // Set accelerometer range and filter bandwidth
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  // Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConnected to Wi-Fi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Get initial accelerometer readings
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  lastX = a.acceleration.x;
  lastY = a.acceleration.y;
  lastZ = a.acceleration.z;
}

// Set up SIM800L module
void setupSIM800L() {
  sendATCommand("AT", 1000);
  sendATCommand("AT+CMGF=1", 1000); // Set SMS to text mode
  sendATCommand("AT+CNMI=1,2,0,0,0", 1000); // Configure SMS notification
}

// Function to send AT commands to SIM800L and wait for response
String sendATCommand(String command, int timeout) {
  Serial.println("Sending command: " + command);
  SIM_SERIAL.println(command);
  
  String response = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    if (SIM_SERIAL.available()) {
      char c = SIM_SERIAL.read();
      response += c;
    }
  }
  
  Serial.println("Response: " + response);
  return response;
}

// Send SMS alert
void sendSMSAlert(float lat, float lon) {
  Serial.println("Sending SMS alert...");
  
  // Format message with GPS coordinates
  String message = "ALERT: Device movement detected!\r\n";
  message += "Location: ";
  message += "https://maps.google.com/?q=";
  message += String(lat, 6);
  message += ",";
  message += String(lon, 6);
  
  // Set recipient phone number
  sendATCommand("AT+CMGS=\"" + String(alertPhoneNumber) + "\"", 1000);
  
  // Send message content and Ctrl+Z to end
  SIM_SERIAL.print(message);
  delay(100);
  SIM_SERIAL.write(26); // Ctrl+Z to send message
  delay(5000); // Wait for message to send
  
  Serial.println("SMS alert sent!");
  alertSent = true;
}

// Send GPS data to server
void sendToServer(float lat, float lon) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Create JSON document
    StaticJsonDocument<200> doc;
    doc["lat"] = lat;
    doc["lon"] = lon;
    doc["deviceId"] = "ESP32-GPS";
    doc["isMoving"] = isMoving;
    
    // Serialize JSON to string
    String jsonData;
    serializeJson(doc, jsonData);
    
    // Print JSON for debugging
    Serial.print("Sending JSON: ");
    Serial.println(jsonData);
    
    // Send POST request with JSON data
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.POST(jsonData);
    
    Serial.print("Server Response: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Response: " + response);
    }
    
    http.end();
  } else {
    Serial.println("Wi-Fi not connected!");
  }
}

// Check for movement from accelerometer
void checkMovement() {
  unsigned long currentTime = millis();
  
  // Only sample at the defined interval
  if (currentTime - lastSampleTime < sampleInterval) {
    return;
  }
  lastSampleTime = currentTime;
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  // Calculate change in acceleration
  float deltaX = abs(a.acceleration.x - lastX);
  float deltaY = abs(a.acceleration.y - lastY);
  float deltaZ = abs(a.acceleration.z - lastZ);
  
  // Update last values
  lastX = a.acceleration.x;
  lastY = a.acceleration.y;
  lastZ = a.acceleration.z;
  
  // Detect movement if change exceeds threshold
  float totalChange = deltaX + deltaY + deltaZ;
  
  if (totalChange > MOVEMENT_THRESHOLD) {
    // Movement detected
    if (!isMoving) {
      // Start timing the movement
      isMoving = true;
      movementStartTime = currentTime;
      Serial.println("Movement detected!");
    }
  } else {
    // No movement detected
    if (isMoving) {
      isMoving = false;
      alertSent = false;
      Serial.println("Movement stopped.");
    }
  }
  
  // Check if movement has continued for more than the threshold time
  if (isMoving && !alertSent && (currentTime - movementStartTime > MOVEMENT_TIME_THRESHOLD)) {
    // Movement has continued for over 30 seconds, send alert if GPS data is available
    if (gps.location.isValid()) {
      sendSMSAlert(gps.location.lat(), gps.location.lng());
    } else {
      Serial.println("Cannot send alert: GPS location not valid");
    }
  }
}

void loop() {
  // Process GPS data
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  
  // Check for movement
  checkMovement();
  
  // Process any SMS responses or status messages from SIM800L
  while (SIM_SERIAL.available()) {
    Serial.write(SIM_SERIAL.read());
  }
  
  // Send GPS location to server periodically
  static unsigned long lastSendTime = 0;
  if (millis() - lastSendTime > 5000) { // Send every 5 seconds
    lastSendTime = millis();
    
    if (gps.location.isUpdated()) {
      float latitude = gps.location.lat();
      float longitude = gps.location.lng();
      
      Serial.print("Latitude: ");
      Serial.print(latitude, 6);
      Serial.print(" | Longitude: ");
      Serial.println(longitude, 6);
      
      sendToServer(latitude, longitude);
    }
  }
}