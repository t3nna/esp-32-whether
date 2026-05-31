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
constexpr uint8_t BUTTON_INPUT_PIN = 0; // ESP32 BOOT button
constexpr uint8_t RESET_INPUT_PIN = 34; // Analog-only input pin
constexpr float TEMP_ALARM_THRESHOLD = 28.0F; // Celsius threshold for the alarm
constexpr TickType_t SENSOR_READ_INTERVAL = pdMS_TO_TICKS(2000);
constexpr TickType_t MONITOR_INTERVAL = pdMS_TO_TICKS(1000); // Check temp every 1s
constexpr TickType_t DISPLAY_REFRESH_INTERVAL = pdMS_TO_TICKS(500);
constexpr TickType_t BUTTON_POLL_INTERVAL = pdMS_TO_TICKS(20);
constexpr TickType_t BUTTON_DEBOUNCE_INTERVAL = pdMS_TO_TICKS(50);
constexpr TickType_t RESET_POLL_INTERVAL = pdMS_TO_TICKS(100);
constexpr uint32_t DEFAULT_BLINK_FREQUENCY_HZ = 10;
constexpr uint32_t BLINK_FREQUENCY_STEP_HZ = 1;
constexpr uint32_t MAX_BLINK_FREQUENCY_HZ = 50;
constexpr uint32_t RESET_THRESHOLD_MV = 2500;

struct SensorData {
  float temperature;
  float humidity;
  bool isValid;
};

Adafruit_BME280 bme; 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SensorData latestSensorData = {0.0F, 0.0F, false};
SemaphoreHandle_t sensorDataMutex = nullptr;
SemaphoreHandle_t blinkFrequencyMutex = nullptr;
uint32_t blinkFrequencyHz = DEFAULT_BLINK_FREQUENCY_HZ;
} // namespace

uint32_t getBlinkFrequencyHz() {
  uint32_t frequencyHz = DEFAULT_BLINK_FREQUENCY_HZ;

  if (xSemaphoreTake(blinkFrequencyMutex, portMAX_DELAY) == pdTRUE) {
    frequencyHz = blinkFrequencyHz;
    xSemaphoreGive(blinkFrequencyMutex);
  }

  return frequencyHz;
}

void setBlinkFrequencyHz(uint32_t newFrequencyHz) {
  const uint32_t boundedFrequencyHz =
      min(max(newFrequencyHz, DEFAULT_BLINK_FREQUENCY_HZ), MAX_BLINK_FREQUENCY_HZ);

  if (xSemaphoreTake(blinkFrequencyMutex, portMAX_DELAY) == pdTRUE) {
    blinkFrequencyHz = boundedFrequencyHz;
    xSemaphoreGive(blinkFrequencyMutex);
  }
}

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
    const uint32_t blinkFrequency = getBlinkFrequencyHz();

    if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE) {
      snapshot = latestSensorData;
      xSemaphoreGive(sensorDataMutex);
    }

    display.clearDisplay(); // Clear previous frame
    display.setTextColor(SSD1306_WHITE);

    if (!snapshot.isValid) {
      display.setTextSize(1);
      display.setCursor(0, 8);
      display.print("Waiting for sensor");
      display.setCursor(0, 20);
      display.print("data...");
      display.setCursor(0, 42);
      display.print("LED Freq: ");
      display.print(blinkFrequency);
      display.print(" Hz");
    } else {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("Temp:");
      display.print(snapshot.temperature, 1);
      display.print(" C");

      display.setCursor(0, 18);
      display.print("Humidity:");
      display.setCursor(0, 28);
      display.print(snapshot.humidity, 1);
      display.print(" %");

      display.setCursor(0, 46);
      display.print("LED Freq: ");
      display.print(blinkFrequency);
      display.print(" Hz");
    }

    display.display(); // Push the drawing to the screen

    vTaskDelay(DISPLAY_REFRESH_INTERVAL);
  }
}

void heartbeatTask(void *pvParameters) {
  (void)pvParameters;

  pinMode(ONBOARD_LED_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED_PIN, LOW);

  while (true) {
    const uint32_t frequencyHz = getBlinkFrequencyHz();
    const uint32_t halfPeriodMs = max<uint32_t>(1, 500U / frequencyHz);

    digitalWrite(ONBOARD_LED_PIN, !digitalRead(ONBOARD_LED_PIN));
    vTaskDelay(pdMS_TO_TICKS(halfPeriodMs));
  }
}

void buttonTask(void *pvParameters) {
  (void)pvParameters;

  pinMode(BUTTON_INPUT_PIN, INPUT_PULLUP);
  bool previousPressed = false;
  TickType_t lastChangeTick = 0;

  while (true) {
    const bool currentPressed = digitalRead(BUTTON_INPUT_PIN) == LOW;
    const TickType_t now = xTaskGetTickCount();

    if (currentPressed != previousPressed &&
        (now - lastChangeTick) >= BUTTON_DEBOUNCE_INTERVAL) {
      previousPressed = currentPressed;
      lastChangeTick = now;

      if (currentPressed) {
        const uint32_t nextFrequencyHz =
            min(getBlinkFrequencyHz() + BLINK_FREQUENCY_STEP_HZ, MAX_BLINK_FREQUENCY_HZ);
        setBlinkFrequencyHz(nextFrequencyHz);
        Serial.print("[BLINK] Frequency increased to ");
        Serial.print(nextFrequencyHz);
        Serial.println(" Hz");
      }
    }

    vTaskDelay(BUTTON_POLL_INTERVAL);
  }
}

void resetInputTask(void *pvParameters) {
  (void)pvParameters;

  pinMode(RESET_INPUT_PIN, INPUT);
  analogSetPinAttenuation(RESET_INPUT_PIN, ADC_11db);
  bool resetLatched = false;

  while (true) {
    const uint32_t resetInputMillivolts = analogReadMilliVolts(RESET_INPUT_PIN);
    const bool thresholdReached = resetInputMillivolts >= RESET_THRESHOLD_MV;

    if (thresholdReached && !resetLatched) {
      setBlinkFrequencyHz(DEFAULT_BLINK_FREQUENCY_HZ);
      Serial.print("[BLINK] Reset to default ");
      Serial.print(DEFAULT_BLINK_FREQUENCY_HZ);
      Serial.print(" Hz (reset input: ");
      Serial.print(resetInputMillivolts);
      Serial.println(" mV)");
      resetLatched = true;
    } else if (!thresholdReached) {
      resetLatched = false;
    }

    vTaskDelay(RESET_POLL_INTERVAL);
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

  blinkFrequencyMutex = xSemaphoreCreateMutex();
  if (blinkFrequencyMutex == nullptr) {
    Serial.println("Failed to create blink frequency mutex!");
    while (1) delay(10);
  }

  Serial.print("Blink control initialized: default=");
  Serial.print(DEFAULT_BLINK_FREQUENCY_HZ);
  Serial.print(" Hz, button GPIO=");
  Serial.print(BUTTON_INPUT_PIN);
  Serial.print(", reset GPIO=");
  Serial.print(RESET_INPUT_PIN);
  Serial.print(", reset threshold=");
  Serial.print(RESET_THRESHOLD_MV / 1000.0F, 1);
  Serial.println(" V");

  // Create dedicated FreeRTOS tasks for reading and rendering.
  xTaskCreate(sensorReadTask, "SensorReadTask", 4096, NULL, 1, NULL);
  xTaskCreate(displayTask, "DisplayTask", 4096, NULL, 1, NULL);
  xTaskCreate(heartbeatTask, "HeartbeatTask", 1024, NULL, 1, NULL);
  xTaskCreate(buttonTask, "ButtonTask", 2048, NULL, 1, NULL);
  xTaskCreate(resetInputTask, "ResetInputTask", 2048, NULL, 1, NULL);
  xTaskCreate(monitorTask, "MonitorTask", 2048, NULL, 2, NULL);
}

void loop() {
  // Empty loop, FreeRTOS handles the logic
}
