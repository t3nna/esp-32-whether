#include <Arduino.h>

int count1 = 0;
int count2 = 0;

void task1(void *pvParameters) {
  while (true) {
    count1++;
    Serial.print("Task 1 count: ");
    Serial.println(count1);
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
  }
}

void task2(void *pvParameters) {
  while (true) {
    count2++;
    Serial.print("Task 2 count: ");
    Serial.println(count2);
    vTaskDelay(500 / portTICK_PERIOD_MS); // Delay for 1 second
  }
}

void setup() {
  Serial.begin(9600);

  xTaskCreate(task1,
     "Task 1",
      1000,
       NULL,
        1,
     NULL);
  xTaskCreate(task2,
     "Task 2",
      1000,
       NULL,
        1,
         NULL);

}

void loop() {

}