#include "locationService.h"
#include <ArduinoJson.h>

const String LocationService::endpoint = "http://192.168.1.187:8080/locations";

LocationService::LocationService(ESP32Time *rtc, WiFiClient *client)
{
    this->rtc = rtc;
    this->client = client;
}

int LocationService::fetchData(LocationDto *locations)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected");
        return 0;
    }
    HTTPClient http;
    http.useHTTP10(true);
    http.begin(*client, LocationService::endpoint);
    int httpResponseCode = http.GET();

    int i = 0;

    if (httpResponseCode > 0)
    {
        Serial.print("Location HTTP Response code: ");
        Serial.println(httpResponseCode);
        JsonDocument doc;
        deserializeJson(doc, http.getStream());
        for (JsonVariant location : doc["locations"].as<JsonArray>())
        {
            locations[i].driverNumer = location["driverNumber"].as<int>();
            locations[i].position = location["location"].as<float>();
            locations[i].occuredAt = location["occurredAt"].as<uint64_t>();
            i++;
        }
    }
    else
    {
        Serial.print("Location error code: ");
        Serial.println(httpResponseCode);
    }
    http.end();
    return i;
}
