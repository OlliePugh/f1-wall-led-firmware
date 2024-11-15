#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <secrets.h>
#include <Hashtable.h>

// Global state
String globalPayload;
SemaphoreHandle_t xMutex;

const char *ssid = SSID;
const char *password = PASSWORD;

// TODO: is the below needed?
IPAddress local_IP(192, 168, 1, 144);
IPAddress gateway(192, 168, 1, 254);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8); // Google DNS

// Your Domain name with URL path or IP address with path
const char *serverName = "http://192.168.1.187:8080/";

WiFiClient client;
HTTPClient http;

struct Location
{
  int occuredAt;
  float position;
};

class Car
{
private:
  static const int MAX_LOCATIONS = 128;
  int startIndex = 0;
  int endIndex = 0;

public:
  uint8_t driverNumber;
  Location locations[MAX_LOCATIONS];
  Car(uint8_t _driverNumber) : driverNumber(_driverNumber) {}

  void addLocation(int occuredAt, float position)
  {
    // this is occured before the data we already have so we don't need it
    if (locations[endIndex].occuredAt > occuredAt)
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
    endIndex = newEndIndex;
    locations[endIndex] = {occuredAt, position};
  }
};

// I really tried to use a hashtable here but the arduino library is ass
Car *cars[20];

void updateLocations(JsonDocument doc)
{
  // Lock the mutex before modifying the global variable
  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
  {
    int i = 0;
    for (JsonVariant location : doc["locations"].as<JsonArray>())
    {
      // Car *car = cars.get(location["driverNumber"].as<int>());
      // if (car == nullptr)
      // {
      //   // Serial.print("Received location for unknown driver: ");
      //   // Serial.println(location["driverNumber"].as<int>());
      // }
      // else
      // {
      //   car->addLocation(location["occuredAt"], location["position"]);
      // }
      i++;
    }
    xSemaphoreGive(xMutex); // Release the mutex
  }
}

void loadLocations()
{
  http.useHTTP10(true);
  http.begin(client, String(serverName) + "locations");
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0)
  {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    JsonDocument doc;
    deserializeJson(doc, http.getStream());
    updateLocations(doc);
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

void httpRequestTask(void *pvParameters)
{
  for (;;)
  {
    loadLocations();
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
  }
}

void loadDrivers()
{
  bool success = false;
  while (!success)
  {
    http.useHTTP10(true);
    http.begin(client, String(serverName) + "drivers");
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
      Serial.print("Driver HTTP Response code: ");
      Serial.println(httpResponseCode);
      JsonDocument doc;
      deserializeJson(doc, http.getStream());
      int i;
      for (JsonVariant driver : doc.as<JsonArray>())
      {
        int driverNumber = driver["driverNumber"].as<int>();
        Car *car = new Car(driverNumber);
        cars[i++] = car;
      }
      success = true;
    }
    else
    {
      Serial.print("Driver error code: ");
      Serial.println(httpResponseCode);
      delay(1000); // Delay for 1 second before retrying
    }

    http.end();
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

  // Create the mutex
  xMutex = xSemaphoreCreateMutex();

  // Get the drivers
  loadDrivers();

  // Create the HTTP request task
  xTaskCreate(httpRequestTask, "HTTP Request Task", 4096, NULL, 1, NULL);
}

void loop()
{
  // Lock the mutex before reading the global variable
  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
  {
    // this should be dynamic
    for (int i = 0; i < 20; i++)
    {
      if (cars[i] != nullptr)
      {
        Serial.print("Driver Number: ");
        Serial.println(cars[i]->driverNumber);
      }
    }

    xSemaphoreGive(xMutex); // Release the mutex
  }

  delay(1000); // Delay for 1 second
}