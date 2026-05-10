#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;

constexpr uint8_t SENSOR_I2C_ADDRESS = 0x76; 
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C; // Most standard I2C address for OLEDs

constexpr uint8_t SCREEN_WIDTH = 128; // OLED display width, in pixels
constexpr uint8_t SCREEN_HEIGHT = 64; // OLED display height, in pixels
constexpr int8_t OLED_RESET = -1;     // Reset pin # (-1 if sharing Arduino reset pin)

constexpr uint32_t SERIAL_BAUD_RATE = 9600;
constexpr uint8_t ONBOARD_LED_PIN = 2; // Usually 2 on standard ESP32 boards
constexpr float TEMP_ALARM_THRESHOLD = 28.0F; // Celsius threshold for the alarm
constexpr TickType_t SENSOR_READ_INTERVAL = pdMS_TO_TICKS(2000);
constexpr TickType_t HEARTBEAT_INTERVAL = pdMS_TO_TICKS(500); // 500ms blink
constexpr TickType_t MONITOR_INTERVAL = pdMS_TO_TICKS(1000); // Check temp every 1s
constexpr TickType_t DISPLAY_REFRESH_INTERVAL = pdMS_TO_TICKS(500);

struct SensorData {
  float temperature;
  float humidity;
  bool isValid;
};

Adafruit_BME280 bme; 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SensorData latestSensorData = {0.0F, 0.0F, false};
SemaphoreHandle_t sensorDataMutex = nullptr;
} // namespace

void sensorReadTask(void *pvParameters) {
  (void)pvParameters;

  while (true) {
    float temp = bme.readTemperature();
    float humidity = bme.readHumidity();
    const bool isValid = !isnan(temp) && !isnan(humidity);

    if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      latestSensorData.temperature = temp;
      latestSensorData.humidity = humidity;
      latestSensorData.isValid = isValid;
      xSemaphoreGive(sensorDataMutex);
    }

    if (!isValid) {
      Serial.println("Failed to read from BME280 sensor!");
      vTaskDelay(SENSOR_READ_INTERVAL);
      continue;
    }

    Serial.print("Temp: ");
    Serial.print(temp, 1);
    Serial.print(" C | Hum: ");
    Serial.print(humidity, 1);
    Serial.println(" %");

    vTaskDelay(SENSOR_READ_INTERVAL);
  }
}

void displayTask(void *pvParameters) {
  (void)pvParameters;

  while (true) {
    SensorData snapshot = {0.0F, 0.0F, false};

    if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      snapshot = latestSensorData;
      xSemaphoreGive(sensorDataMutex);
    }

    display.clearDisplay(); // Clear previous frame
    display.setTextColor(SSD1306_WHITE);

    if (!snapshot.isValid) {
      display.setTextSize(1);
      display.setCursor(0, 10);
      display.print("Waiting for sensor");
      display.setCursor(0, 24);
      display.print("data...");
    } else {
      // Temperature Header
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Temperature:");

      // Temperature Value
      display.setTextSize(2); // Make the numbers larger
      display.setCursor(0, 12);
      display.print(snapshot.temperature, 1);
      display.print(" C");

      // Humidity Header
      display.setTextSize(1);
      display.setCursor(0, 34);
      display.print("Humidity:");

      // Humidity Value
      display.setTextSize(2);
      display.setCursor(0, 46);
      display.print(snapshot.humidity, 1);
      display.print(" %");
    }

    display.display(); // Push the drawing to the screen

    vTaskDelay(DISPLAY_REFRESH_INTERVAL);
  }
}

void heartbeatTask(void *pvParameters) {
  (void)pvParameters;

  pinMode(ONBOARD_LED_PIN, OUTPUT);

  while (true) {
    digitalWrite(ONBOARD_LED_PIN, !digitalRead(ONBOARD_LED_PIN));
    vTaskDelay(HEARTBEAT_INTERVAL);
  }
}

void monitorTask(void *pvParameters) {
  (void)pvParameters;

  while (true) {
    SensorData snapshot = {0.0F, 0.0F, false};

    if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      snapshot = latestSensorData;
      xSemaphoreGive(sensorDataMutex);
    }

    if (snapshot.isValid && snapshot.temperature > TEMP_ALARM_THRESHOLD) {
      Serial.println("[ALARM] High Temperature Detected!");
    }

    vTaskDelay(MONITOR_INTERVAL);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.setDebugOutput(false);
  delay(1000);
  
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("Initializing hardware...");

  // Initialize the BME280
  if (!bme.begin(SENSOR_I2C_ADDRESS, &Wire)) {
    Serial.println("Could not find a valid BME280 sensor!");
    while (1) delay(10);
  }
  Serial.println("BME280 Initialized.");

  // Initialize the OLED Display
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println("SSD1306 allocation failed. Check OLED wiring!");
    while (1) delay(10);
  }
  Serial.println("OLED Initialized.");

  // Clear display buffer and set initial state
  display.clearDisplay();
  display.display();

  sensorDataMutex = xSemaphoreCreateMutex();
  if (sensorDataMutex == nullptr) {
    Serial.println("Failed to create sensor data mutex!");
    while (1) delay(10);
  }

  // Create dedicated FreeRTOS tasks for reading and rendering.
  xTaskCreate(sensorReadTask, "SensorReadTask", 4096, NULL, 1, NULL);
  xTaskCreate(displayTask, "DisplayTask", 4096, NULL, 1, NULL);
  xTaskCreate(heartbeatTask, "HeartbeatTask", 1024, NULL, 1, NULL);
  xTaskCreate(monitorTask, "MonitorTask", 2048, NULL, 2, NULL);
}

void loop() {
  // Empty loop, FreeRTOS handles the logic
}
