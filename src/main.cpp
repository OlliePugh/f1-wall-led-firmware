#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <secrets.h>

#include "locationService.h"
#include "driverService.h"
#include "timeService.h"

const char *ssid = SSID;
const char *password = PASSWORD;

struct Location
{
  uint64_t occurredAt;
  float position;
};

WiFiClient client;
ESP32Time rtc(0); // offset in seconds GMT+1
LocationService locationService(&rtc, &client);
DriverService driverService(&client);
TimeService timeService(&rtc);

bool offsetSet = false;

class Car
{
private:
  static const int MAX_LOCATIONS = 128;
  int startIndex = 0;
  int endIndex = 0;
  Location locations[MAX_LOCATIONS];
  SemaphoreHandle_t xMutex;

public:
  uint8_t driverNumber;
  Car(uint8_t _driverNumber) : driverNumber(_driverNumber)
  {
    xMutex = xSemaphoreCreateMutex();
    for (int i = 0; i < MAX_LOCATIONS; i++)
    {
      locations[i] = {0, 0};
    }
  }

  void addLocation(uint64_t occurredAt, float position)
  {
    // this is occurred before the data we already have so we don't need it
    if (locations[endIndex].occurredAt > occurredAt)
    {
      return;
    }

    // if the end index would mean that the end index would be the same as the start index
    // and start overwriting the data
    int newEndIndex = (endIndex + 1) % MAX_LOCATIONS;

    if (newEndIndex == startIndex)
    {
      Serial.println("Buffer full");
      return;
    }
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      locations[endIndex] = {occurredAt, position};
      xSemaphoreGive(xMutex);
    }
    endIndex = newEndIndex;
  }

  Location getLocation()
  {
    Location loc;
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      // get the current time from the rtc
      uint64_t currentTime = timeService.getTime();

      // find first location that is just before the current time
      int i = startIndex;
      while (locations[i].occurredAt < currentTime && i != endIndex)
      {
        Serial.println("skipping");
        i = (i + 1) % MAX_LOCATIONS;
      }

      startIndex = i;

      loc = locations[i];
      xSemaphoreGive(xMutex);
    }
    return loc;
  }
};

const int CARS_BUFFER_SIZE = 30;
// I really tried to use a hashtable here but the arduino library is ass
Car *cars[CARS_BUFFER_SIZE];

Car *getCarByNumber(int driverNumber)
{
  for (int i = 0; i < CARS_BUFFER_SIZE; i++)
  {
    if (cars[i] != nullptr && cars[i]->driverNumber == driverNumber)
    {
      return cars[i];
    }
  }
  Serial.print("cat not available with number ");
  Serial.println(driverNumber);
  return nullptr;
}

void updateLocations(LocationDto *locations, int amountInBuffer)
{
  for (size_t i = 0; i < amountInBuffer; i++)
  {
    LocationDto currentLocation = locations[i];
    if (!offsetSet)
    {
      // i.e. we are running an simulation as opposte to realtime
      if (timeService.getTime() - currentLocation.occuredAt > 100000)
      {
        Serial.println("Updating local time to match server");
        timeService.setServerTime(currentLocation.occuredAt);
      }
      offsetSet = true;
    }

    // TODO: ONYL TESTING WITH DRIVER ONE
    if (currentLocation.driverNumer != 1)
    {
      return;
    }

    Car *car = getCarByNumber(currentLocation.driverNumer);

    // TODO: check if the driver number exists
    if (car != nullptr)
    {
      car->addLocation(currentLocation.occuredAt, currentLocation.position);
    }
  }
}

void httpRequestTask(void *pvParameters)
{
  LocationDto buffer[1500];
  while (true)
  {
    int newLocations = locationService.fetchData(buffer);
    updateLocations(buffer, newLocations);
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
  }
}

void loadDrivers()
{
  uint8_t driverNumbers[30];
  int driverCount = 0;
  while (driverCount == 0)
  {
    driverCount = driverService.fetchData(driverNumbers);
  }
  for (int i = 0; i < driverCount; i++)
  {
    cars[i] = new Car(driverNumbers[i]);
  }
}

void setup()
{
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");

  timeService.setup();
  loadDrivers();

  // Create the HTTP request task
  xTaskCreate(httpRequestTask, "HTTP Request Task", 32768, NULL, 1, NULL);
}

LocationDto buffer[4096];
void loop()
{

  Car *car = cars[0];

  Location location = car->getLocation();

  Serial.print("driver number: ");
  Serial.print(cars[0]->driverNumber);
  Serial.print(" position: ");
  Serial.println(location.position);

  // for (int i = 0; i < CARS_BUFFER_SIZE; i++)
  // {
  //   if (cars[i] != nullptr)
  //   {
  //     Car *car = cars[i];

  //     Location location = car->getLocation();

  //     Serial.print("driver number: ");
  //     Serial.print(cars[i]->driverNumber);
  //     Serial.print(" position: ");
  //     Serial.println(location.position);
  //   }
  // }
  // delay(2000); // Delay for 2 seconds
}
