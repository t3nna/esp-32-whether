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
constexpr TickType_t SENSOR_READ_INTERVAL = pdMS_TO_TICKS(2000);

Adafruit_BME280 bme; 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
} // namespace

void sensorAndDisplayTask(void *pvParameters) {
  (void)pvParameters;

  while (true) {
    // 1. Read data from the BME280
    float temp = bme.readTemperature();
    float humidity = bme.readHumidity();

    // Check if reads failed
    if (isnan(temp) || isnan(humidity)) {
      Serial.println("Failed to read from BME280 sensor!");
      
      display.clearDisplay();
      display.setCursor(0, 10);
      display.setTextSize(1);
      display.print("Sensor Error!");
      display.display();
      
      vTaskDelay(SENSOR_READ_INTERVAL);
      continue;
    }

    // 2. Print to Serial Monitor
    Serial.print("Temp: ");
    Serial.print(temp, 1);
    Serial.print(" C | Hum: ");
    Serial.print(humidity, 1);
    Serial.println(" %");

    // 3. Update the OLED Display
    display.clearDisplay(); // Clear previous frame
    display.setTextColor(SSD1306_WHITE);

    // Temperature Header
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Temperature:");

    // Temperature Value
    display.setTextSize(2); // Make the numbers larger
    display.setCursor(0, 12);
    display.print(temp, 1); 
    display.print(" C");

    // Humidity Header
    display.setTextSize(1);
    display.setCursor(0, 34);
    display.print("Humidity:");

    // Humidity Value
    display.setTextSize(2);
    display.setCursor(0, 46);
    display.print(humidity, 1);
    display.print(" %");

    display.display(); // Push the drawing to the screen

    vTaskDelay(SENSOR_READ_INTERVAL);
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

  // Create the FreeRTOS task
  xTaskCreate(sensorAndDisplayTask, "SensorDisplayTask", 4096, NULL, 1, NULL);
}

void loop() {
  // Empty loop, FreeRTOS handles the logic
}