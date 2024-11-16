#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <secrets.h>
#include <Hashtable.h>
#include <ESP32Time.h>

#include "locationService.h"
#include "driverService.h"

// Global state
String globalPayload;

const char *ssid = SSID;
const char *password = PASSWORD;

// TODO: is the below needed?
IPAddress local_IP(192, 168, 1, 144);
IPAddress gateway(192, 168, 1, 254);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8); // Google DNS

struct Location
{
  uint64_t occurredAt;
  float position;
};

WiFiClient client;
ESP32Time rtc(0); // offset in seconds GMT+1
LocationService locationService(&rtc, &client);
DriverService driverService(&client);

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
      loc = locations[0];
      xSemaphoreGive(xMutex);
    }
    return loc;
  }
};

const int CARS_BUFFER_SIZE = 30;
// I really tried to use a hashtable here but the arduino library is ass
Car *cars[CARS_BUFFER_SIZE];

uint64_t getCurrentTime()
{
  return static_cast<uint64_t>(rtc.getEpoch()) * 1000ULL + rtc.getMillis();
}

Car *getCarByNumber(int driverNumber)
{
  for (int i = 0; i < CARS_BUFFER_SIZE; i++)
  {
    if (cars[i] != nullptr && cars[i]->driverNumber == driverNumber)
    {
      return cars[i];
    }
  }
  return nullptr;
}

void setupDateTime()
{
  configTime(0, 0, "time.google.com");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    rtc.setTimeStruct(timeinfo);
  }
}

void updateLocations(LocationDto *locations, int amountInBuffer)
{
  for (size_t i = 0; i < amountInBuffer; i++)
  {
    LocationDto currentLocation = locations[i];
    Car *car = getCarByNumber(currentLocation.driverNumer);

    // TODO: check if the driver number exists
    car->addLocation(currentLocation.occuredAt, currentLocation.position);
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

  // Setup the date and time
  setupDateTime();

  // Get the drivers
  loadDrivers();

  // Create the HTTP request task
  xTaskCreate(httpRequestTask, "HTTP Request Task", 32768, NULL, 1, NULL);
}

LocationDto buffer[4096];
void loop()
{

  for (int i = 0; i < CARS_BUFFER_SIZE; i++)
  {
    if (cars[i] != nullptr)
    {
      Car *car = cars[i];

      Location location = car->getLocation();

      Serial.print("driver number: ");
      Serial.print(cars[i]->driverNumber);
      Serial.print(" position: ");
      Serial.println(location.position);
    }
  }
  delay(2000); // Delay for 1 second
}
