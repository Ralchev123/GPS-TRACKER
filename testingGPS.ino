#include <TinyGPS++.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#define RXD2 14
#define TXD2 12
#define GPS_BAUD 9600

#define SDA_PIN 21
#define SCL_PIN 22


#define SIM_SERIAL Serial1

#define MOVEMENT_THRESHOLD 0.5
#define MOVEMENT_TIME_THRESHOLD 5000 // 30 seconds in milliseconds

const char* ssid = "Insert_Your_Wifi_Network_Here";
const char* password = "PASSWORD";
const char* serverURL = "http://LOCAL_IP:3000/api/data";

const char* alertPhoneNumber = "PHONE NUMBER";

HardwareSerial gpsSerial(2);
TinyGPSPlus gps;
Adafruit_MPU6050 mpu;

bool isMoving = false;
unsigned long movementStartTime = 0;
bool alertSent = false;
float lastX, lastY, lastZ;
unsigned long lastSampleTime = 0;
const int sampleInterval = 100; 

void setup() {
  Serial.begin(115200);
  
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  Serial.println("GPS module started");
  
  SIM_SERIAL.begin(115200); 
  delay(3000); 
  Serial.println("SIM800L module started");
  setupSIM800L();
  
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!mpu.begin(0x68, &Wire)) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 accelerometer started");
  
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConnected to Wi-Fi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  lastX = a.acceleration.x;
  lastY = a.acceleration.y;
  lastZ = a.acceleration.z;
}

void setupSIM800L() {
  sendATCommand("AT", 1000);
  sendATCommand("AT+CMGF=1", 1000); 
  sendATCommand("AT+CNMI=1,2,0,0,0", 1000);
}

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

void sendSMSAlert(float lat, float lon) {
  Serial.println("Sending SMS alert...");
  
  String message = "ALERT: Device movement detected!\r\n";
  message += "Location: ";
  message += "https://maps.google.com/?q=";
  message += String(lat, 6);
  message += ",";
  message += String(lon, 6);
  
  sendATCommand("AT+CMGS=\"" + String(alertPhoneNumber) + "\"", 1000);
  
  SIM_SERIAL.print(message);
  delay(100);
  SIM_SERIAL.write(26); 
  delay(5000); 
  
  Serial.println("SMS alert sent!");
  alertSent = true;
}

void sendToServer(float lat, float lon) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    StaticJsonDocument<200> doc;
    doc["lat"] = lat;
    doc["lon"] = lon;
    doc["deviceId"] = "ESP32-GPS";
    doc["isMoving"] = isMoving;
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    Serial.print("Sending JSON: ");
    Serial.println(jsonData);
    
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

void checkMovement() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastSampleTime < sampleInterval) {
    return;
  }
  lastSampleTime = currentTime;
  
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  float deltaX = abs(a.acceleration.x - lastX);
  float deltaY = abs(a.acceleration.y - lastY);
  float deltaZ = abs(a.acceleration.z - lastZ);
  
  lastX = a.acceleration.x;
  lastY = a.acceleration.y;
  lastZ = a.acceleration.z;
  
  float totalChange = deltaX + deltaY + deltaZ;
  
  if (totalChange > MOVEMENT_THRESHOLD) {
    if (!isMoving) {
      isMoving = true;
      movementStartTime = currentTime;
      Serial.println("Movement detected!");
    }
  } else {
    if (isMoving) {
      isMoving = false;
      alertSent = false;
      Serial.println("Movement stopped.");
    }
  }
  
  if (isMoving && !alertSent && (currentTime - movementStartTime > MOVEMENT_TIME_THRESHOLD)) {
    if (gps.location.isValid()) {
      sendSMSAlert(gps.location.lat(), gps.location.lng());
    } else {
      Serial.println("Cannot send alert: GPS location not valid");
    }
  }
}

void loop() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  
  checkMovement();
  
  while (SIM_SERIAL.available()) {
    Serial.write(SIM_SERIAL.read());
  }
  
  static unsigned long lastSendTime = 0;
  if (millis() - lastSendTime > 5000) { 
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
