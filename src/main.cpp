#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <secrets.h>
#include <FastLED.h>

#include "locationService.h"
#include "driverService.h"
#include "timeService.h"

#define NUM_LEDS 297
#define DATA_PIN 19
CRGB leds[NUM_LEDS];

#define LEADING_AFFECT 3.0f
#define SMOOTHING_ENABLED false

SET_LOOP_TASK_STACK_SIZE(16 * 1024); // 16KB

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
  float lastRequestedLocation = -1;

public:
  uint8_t driverNumber;
  String name;
  CRGB color;
  Car(uint8_t _driverNumber, String _name, CRGB _color) : driverNumber(_driverNumber), name(_name), color(_color)
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

  float getLocation()
  {
    removeOutdatedLocations();
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      // get last location
      Location *previous = locations[0];
      Location *next = locations[1];

      if (previous == nullptr)
      {
        xSemaphoreGive(xMutex);
        return lastRequestedLocation;
      }

      if (next == nullptr)
      {
        float position = previous->position;
        xSemaphoreGive(xMutex);
        lastRequestedLocation = position;
        return position;
      }

      // get the current time from the rtc
      uint64_t currentTime = timeService.getTime();
      Location *loc = nullptr;
      // check if the current time is between the two locations
      if (previous->occurredAt <= currentTime && currentTime <= next->occurredAt)
      {
        // calculate the position between the two locations
        float position;
        if (previous->position > 90 && next->position < 10)
        {
          // handle wrap-around from 99 to 1
          position = previous->position + (next->position + 100 - previous->position) * (currentTime - previous->occurredAt) / (next->occurredAt - previous->occurredAt);
          if (position >= 100)
          {
            position -= 100;
          }
        }
        else
        {
          position = previous->position + (next->position - previous->position) * (currentTime - previous->occurredAt) / (next->occurredAt - previous->occurredAt);
        }
        xSemaphoreGive(xMutex);
        lastRequestedLocation = position;
        return position;
      }
      xSemaphoreGive(xMutex);
    }

    return lastRequestedLocation;
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
    cars[i] = new Car(driverNumbers[i].driverNumber, driverNumbers[i].name, driverNumbers[i].color);
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

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.show();

  // Create the HTTP request task
  xTaskCreate(httpRequestTask, "HTTP Request Task", 32768, NULL, 1, NULL);
}

LocationDto buffer[4096];
void loop()
{
  FastLED.show();
  // set all leds to white
  for (int i = 0; i < NUM_LEDS; i++)
  {
    leds[i].setRGB(50, 50, 50);
  }

  for (int i = 0; i < CARS_BUFFER_SIZE; i++)
  {
    if (cars[i] != nullptr)
    {
      float loc = cars[i]->getLocation();

      if (loc != -1)
      {

        if (SMOOTHING_ENABLED)
        {
          float ledPosition = ((loc / 100) * NUM_LEDS);

          int ledIndex = ledPosition;

          ledIndex = max(0, min(NUM_LEDS - 1, ledIndex));

          // calculate the fractional part of the location
          float fractionalPart = ledPosition - ledIndex;

          CRGB following = CRGB(cars[i]->color).fadeLightBy(255 * pow(fractionalPart, LEADING_AFFECT));
          CRGB leading = CRGB(cars[i]->color).fadeLightBy(255 * pow(1.0f - fractionalPart, LEADING_AFFECT));

          // set the led to the color of the driver with the appropriate brightness
          leds[ledIndex] += following;

          // set the next led to the color of the driver with the remaining brightness
          leds[(ledIndex + 1) % NUM_LEDS] += leading;
        }
        else
        {

          float ledPosition = ((loc / 100) * NUM_LEDS);

          int ledIndex = ledPosition;

          ledIndex = max(0, min(NUM_LEDS - 1, ledIndex));

          leds[ledIndex] = cars[i]->color;
        }
      }
      else
      {
        Serial.print("No valid location for car number ");
        Serial.println(cars[i]->driverNumber);
      }
    }
  }
  delay(1000 / 60);
}
