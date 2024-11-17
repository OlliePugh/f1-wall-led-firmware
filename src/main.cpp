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
  Location *locations[MAX_LOCATIONS];
  SemaphoreHandle_t xMutex;

public:
  uint8_t driverNumber;
  String name;
  Car(uint8_t _driverNumber, String _name) : driverNumber(_driverNumber), name(_name)
  {
    xMutex = xSemaphoreCreateMutex();
    for (int i = 0; i < MAX_LOCATIONS; i++)
    {
      locations[i] = nullptr;
    }
  }

  void removeOutdatedLocations()
  {
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      // get the current time from the rtc
      uint64_t currentTime = timeService.getTime();

      // Remove outdated locations
      int lastValidIndex = -1;
      for (int i = 0; i < MAX_LOCATIONS; i++)
      {
        if (locations[i] != nullptr && locations[i]->occurredAt < currentTime)
        {
          lastValidIndex = i;
        }
      }

      for (int i = 0; i < lastValidIndex; i++)
      {
        if (locations[i] != nullptr)
        {
          // Serial.println("removing location because it is in the past");
          delete locations[i];
          locations[i] = nullptr;
        }
      }

      // Shift all non-null pointers to the left
      int shiftIndex = 0;
      for (int i = 0; i < MAX_LOCATIONS; i++)
      {
        if (locations[i] != nullptr)
        {
          locations[shiftIndex++] = locations[i];
        }
      }

      // Set the remaining pointers to nullptr
      for (int i = shiftIndex; i < MAX_LOCATIONS; i++)
      {
        locations[i] = nullptr;
      }

      xSemaphoreGive(xMutex);
    }
    else
    {
      Serial.println("could not take mutex");
    }
  }

  void addLocation(uint64_t occurredAt, float position)
  {
    removeOutdatedLocations();
    // if the data is in the past we can ignore it
    if (occurredAt < timeService.getTime())
    {
      return;
    }

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      // check if this location is already in the queue
      for (int i = 0; i < MAX_LOCATIONS; i++)
      {
        if (locations[i] != nullptr &&
            locations[i]->occurredAt == occurredAt &&
            locations[i]->position == position)
        {
          // Serial.println("Location already exists in the queue");
          // nothing to do
          xSemaphoreGive(xMutex);
          return;
        }
      }

      bool successfull = false;
      // add it to the end of the locations queue
      for (int i = 0; i < MAX_LOCATIONS; i++)
      {
        if (locations[i] == nullptr)
        {
          locations[i] = new Location{occurredAt, position};
          successfull = true;
          break;
        }
      }

      if (!successfull)
      {
        Serial.println("Buffer full");
      }

      xSemaphoreGive(xMutex);
    }
    else
    {
      Serial.println("could not take mutex");
    }
  }

  Location *getLocation()
  {
    removeOutdatedLocations();
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      Location *loc = locations[0];
      xSemaphoreGive(xMutex);
      return loc;
    }

    return nullptr;
    //   // get the current time from the rtc
    //   uint64_t currentTime = timeService.getTime();

    //   // find first location that is just before the current time
    //   while (!locations.isEmpty() && locations.getHead().occurredAt < currentTime)
    //   {
    //     Serial.println("removing location because it is in the past");
    //     locations.dequeue();
    //   }

    //   if (locations.isEmpty())
    //   {
    //     xSemaphoreGive(xMutex);
    //     Serial.println("No locations available");
    //     return nullptr;
    //   }

    //   loc = locations.getHeadPtr();
    //   if (loc == nullptr)
    //   {
    //     xSemaphoreGive(xMutex);
    //     Serial.println("current location is null");
    //     return nullptr;
    //   }
    //   Serial.print("event time ");
    //   Serial.println(loc->occurredAt);
    //   Serial.print("current time ");
    //   Serial.println(currentTime);

    //   xSemaphoreGive(xMutex);
    // }
    // else
    // {
    //   Serial.println("could not take mutex");
    // }
    // return loc;
  }

  void debug()
  {
    Serial.print("Driver number: ");
    Serial.println(driverNumber);
    Serial.print("====== LOCATION DUMP (CAR ");
    Serial.print(driverNumber);
    Serial.println(") ======");

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      // Print the locations from the array
      for (int i = 0; i < MAX_LOCATIONS; i++)
      {
        if (locations[i] != nullptr)
        {
          Serial.print("occurred at: ");
          Serial.print(locations[i]->occurredAt);
          Serial.print(" position: ");
          Serial.println(locations[i]->position);
        }
      }

      xSemaphoreGive(xMutex);
    }
    else
    {
      Serial.println("could not take mutex");
    }
    Serial.println("====== END LOCATION DUMP ======");
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

    Car *car = getCarByNumber(currentLocation.driverNumer);

    // TODO: check if the driver number exists
    if (car != nullptr)
    {
      car->addLocation(currentLocation.occuredAt, currentLocation.position);
      // car->debug();
    }
    else
    {
      Serial.print("car not found with number ");
      Serial.println(currentLocation.driverNumer);
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
  Driver driverNumbers[30];
  int driverCount = 0;
  while (driverCount == 0)
  {
    driverCount = driverService.fetchData(driverNumbers);
  }
  for (int i = 0; i < driverCount; i++)
  {
    cars[i] = new Car(driverNumbers[i].driverNumber, driverNumbers[i].name);
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
  for (int i = 0; i < CARS_BUFFER_SIZE; i++)
  {
    if (cars[i] != nullptr)
    {
      Location *loc = cars[i]->getLocation();
      if (loc != nullptr)
      {
        Serial.print(">");
        Serial.print(cars[i]->name);
        Serial.print(":");
        Serial.println(loc->position);
      }
      else
      {
        Serial.println("No valid location");
      }
    }
  }
}
