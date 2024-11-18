#include "driverService.h"
#include <ArduinoJson.h>

const String DriverService::endpoint = "http://192.168.1.187:8080/drivers";

DriverService::DriverService(WiFiClient *client)
{
    this->client = client;
}

int DriverService::fetchData(Driver *drivers)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected");
        return 0;
    }
    HTTPClient http;
    http.useHTTP10(true);
    http.begin(*client, DriverService::endpoint);
    int httpResponseCode = http.GET();

    int i = 0;

    if (httpResponseCode > 0)
    {
        Serial.print("Driver HTTP Response code: ");
        Serial.println(httpResponseCode);
        JsonDocument doc;
        deserializeJson(doc, http.getStream());
        for (JsonVariant driver : doc.as<JsonArray>())
        {
            int driverNumber = driver["driverNumber"].as<int>();
            String name = driver["nameAcronym"].as<String>();
            String colorString = driver["teamColor"].as<String>();
            int r = strtol(colorString.substring(0, 2).c_str(), NULL, 16);
            int g = strtol(colorString.substring(2, 4).c_str(), NULL, 16);
            int b = strtol(colorString.substring(4, 6).c_str(), NULL, 16);
            CRGB color = CRGB(r, g, b);
            Driver driverInstance = {driverNumber, name, color};
            drivers[i++] = driverInstance;
        }
    }
    else
    {
        Serial.print("Driver error code: ");
        Serial.println(httpResponseCode);
    }
    Serial.println("Finished fetching drivers");
    http.end();
    return i;
}