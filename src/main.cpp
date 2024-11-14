#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include <secrets.h>

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
const char *serverName = "http://192.168.1.187:8080/locations";
// const char *serverName = "http://google.co.uk/";

WiFiClient client;
HTTPClient http;

struct Location
{
  int driverNumber;
  int occuredAt;
  float position;
};

// don't store as locations, have a car class that stores their own locations and then ask for their current
// position and this should calculate all of the interpolation etc
// then store these as a map<driverNumber, driver>
Location locations[1024];

void updateLocations(JsonDocument doc)
{
  // Lock the mutex before modifying the global variable
  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
  {
    int i = 0;
    for (JsonVariant location : doc["locations"].as<JsonArray>())
    {
      locations[i].driverNumber = location["driverNumber"];
      locations[i].position = location["location"];
      locations[i].occuredAt = location["date"];
      i++;
    }
    xSemaphoreGive(xMutex); // Release the mutex
  }
}

void httpRequest()
{
  http.useHTTP10(true);
  http.begin(client, serverName);
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
    httpRequest();
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
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

  // Create the HTTP request task
  xTaskCreate(httpRequestTask, "HTTP Request Task", 4096, NULL, 1, NULL);
}

void loop()
{
  // Lock the mutex before reading the global variable
  if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
  {
    Serial.print(">max:");

    // find first value in array with driver number 1
    for (int i = 0; i < 1024; i++)
    {
      if (locations[i].driverNumber == 1)
      {
        Serial.println(String(locations[i].position));
        break;
      }
    }
    xSemaphoreGive(xMutex); // Release the mutex
  }

  delay(1000); // Delay for 1 second
}