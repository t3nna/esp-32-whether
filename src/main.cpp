#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

namespace {
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
// Renamed to avoid collision with Adafruit's internal macros
constexpr uint8_t SENSOR_I2C_ADDRESS = 0x76; 
constexpr uint32_t SERIAL_BAUD_RATE = 9600;
constexpr TickType_t SENSOR_READ_INTERVAL = pdMS_TO_TICKS(2000);

Adafruit_BME280 bme; // I2C
} // namespace

void sensorTask(void *pvParameters) {
  (void)pvParameters;

  while (true) {
    // Read data from the BME280
    float temp = bme.readTemperature();
    float humidity = bme.readHumidity();
    float pressure = bme.readPressure() / 100.0F; // Convert Pa to hPa

    // Check if any reads failed and exit early (to try again)
    if (isnan(temp) || isnan(humidity) || isnan(pressure)) {
      Serial.println("Failed to read from BME280 sensor!");
      vTaskDelay(SENSOR_READ_INTERVAL);
      continue;
    }

    Serial.print("Temperature: ");
    Serial.print(temp, 2);
    Serial.print(" °C | ");

    Serial.print("Humidity: ");
    Serial.print(humidity, 2);
    Serial.print(" % | ");

    Serial.print("Pressure: ");
    Serial.print(pressure, 2);
    Serial.println(" hPa");

    vTaskDelay(SENSOR_READ_INTERVAL);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.setDebugOutput(false);
  delay(1000);
  
  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("Starting HW-611 (BME280) sensor task...");

  // Initialize the BME280 sensor using the renamed variable
  bool status = bme.begin(SENSOR_I2C_ADDRESS, &Wire);  
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor at 0x76, check wiring!");
    Serial.print("SensorID was: 0x"); 
    Serial.println(bme.sensorID(), HEX);
    return;
  }

  Serial.println("BME280 initialized successfully.");

  // Create the FreeRTOS task to read the sensor
  xTaskCreate(sensorTask, "SensorTask", 4096, NULL, 1, NULL);
}

void loop() {
  // Empty loop, FreeRTOS task handles the reads
}