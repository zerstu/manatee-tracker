/*
 * manateeTracking-v1
 * Description: Code for Manatee Tracking - rev1
 * Author: Kevin Xu
 * Date: 9 Jul 2019
 */

// Getting necessary libraries
// Serial4 is included here in order to change C2/C3 to RX/TX UART pins, respectively
// This is because the AssetTracker v2 already occupies Serial1 (RX/TX)
#include "AssetTrackerRK.h"
#include "IridiumSBD.h"
#include "Serial4/Serial4.h"
#include "TinyGPS++.h"

SYSTEM_THREAD(ENABLED);

// Function declaration
void displayInfo();

const unsigned long PUBLISH_PERIOD = 5 * 60 * 1000; // 5 minute delay
const unsigned long MAX_GPS_AGE_MS = 10000;

// TinyGPS++ object declaration
TinyGPSPlus gps;
unsigned long lastSerial = 0;
unsigned long lastPublish = 0;
bool gettingFix = false;

// FuelGauge declaration for 'batt' function
FuelGauge fuel;

#define AssetTrackerSerial Serial1

#define IridiumSerial Serial4
IridiumSBD modem(IridiumSerial, D0);

void setup()
{
    Particle.disconnect();

    // Functions will allow you to instantly ping a location and also get battery updates
    // Functions can be reached through the Particle console
    // Alternatively, you can download the Particle CLI and call the function
    // Calling the function uses the format $ particle call (deviceName) (functionName) ex. $ particle call ManateeTracker batt
    Particle.function("batt", batteryStatus);
    Particle.function("gps", gpsPublish);
    Particle.function("test", iridiumTest);

    Serial.begin(9600);

    AssetTrackerSerial.begin(9600);
    IridiumSerial.begin(19200);
    modem.begin();

    // Power up the GPS module
    // Setting D6 LOW begins the Asset Tracker (t.gpsOn)
    pinMode(D6, OUTPUT);
    digitalWrite(D6, LOW);
    gettingFix = true;
}

void loop()
{
    // Keep running while the AssetTracker is reading GPS coordinates
    // TinyGPS++ requires the encode() function in order to 'feed' the stream of data coming from the Asset Tracker
    // This fed NMEA data can then be used to display the GPS coordinates of the object
    // This occurs in the function displayInfo()
    while (AssetTrackerSerial.available() > 0) {
        if (gps.encode(AssetTrackerSerial.read())) {
            displayInfo();
        }
    }
}

void displayInfo()
{
    // Make sure that the current time - the last time a coordinate was taken > the set delay (PUBLISH_PERIOD)
    if (millis() - lastSerial >= PUBLISH_PERIOD) {
        // Capture the current time when this GPS coordinate is being taken
        lastSerial = millis();

        // Declare a buffer in order to store the message (coordinate or failed attempt) that is to be sent
        char buf[128];

        // Make sure that the location obtained by the GPS is valid and also new
        if (gps.location.isValid()) {
            // Print data to the buffer (latitude and longitude)
            snprintf(buf, sizeof(buf), "%f, %f", gps.location.lat(), gps.location.lng());
        }
        else {
            // If either of the above conditions in the if statement fail, then we want to output that no location was obtained
            strcpy(buf, "no location");
        }

        // If the Particle Electron is connected to the cloud, then we want to publish the GPS coordinates to the Particle cloud
        // In this case, the event name is called G, we publish buf (which either contains our coordinates or 'no location'), and the event is private to us
        // Otherwise, we want to send the message through the Iridium Satellite system (rockBLOCK 9603)
        // Sending through the rockBLOCK may take a couple minutes
        if (Particle.connected()) {
            lastPublish = millis();
            Particle.publish("G", buf, PRIVATE);
            modem.sendSBDText(buf);
        }
        else {
          modem.sendSBDText(buf);
        }
    }
}

// This function allows you to ping the Electron in order to determine its battery status
// In the Events tab, the output is displayed under event name B and displays the battery voltage as well as the % of charge remaining
// The function returns 1 in the CLI if the battery percentage exceeds 10%
// The function returns 0 otherwise
int batteryStatus(String command)
{
  Particle.publish("B", "v:" + String::format("%.2f", fuel.getVCell())
      + ", c:" + String::format("%.2f", fuel.getSoC()),
      60, PRIVATE);

  if (fuel.getSoC() > 10) {
    return 1;
  }
  else {
    return 0;
  }
}

// This function mimics displayInfo() pretty closely and allows you to ping the Electron for its current location
// However, this function does not reset the timer of lastSerial, thus the program continues to run as normal
// The function returns 0 if no location/GPS fix is obtained
// The function returns 1 if the GPS coordinate was transmitted to the Particle cloud
// The function returns 2 if the GPS coordinate was transmitted to the Iridium satellite network
int gpsPublish(String command)
{
    while (AssetTrackerSerial.available() > 0)
        gps.encode(AssetTrackerSerial.read());

    char buf[128];
    if (gps.location.isValid() && gps.location.age() < MAX_GPS_AGE_MS) {
        snprintf(buf, sizeof(buf), "%f, %f, %f", gps.location.lat(), gps.location.lng());
    }
    else {
        strcpy(buf, "no location");
        Particle.publish("G", buf, PRIVATE);
        return 0;
    }

    if (Particle.connected()) {
        lastPublish = millis();
        Particle.publish("G", buf, PRIVATE);
        return 1;
    }
    else {
        modem.sendSBDText(buf);
        return 2;
    }
}

int iridiumTest(String command)
{
  int err = modem.sendSBDText("Hello, world!");

  if(err != ISBD_SUCCESS) {
    Particle.publish("error", "sendSBDText failed: error", PRIVATE);
    if (err == ISBD_SENDRECEIVE_TIMEOUT)
      Particle.publish("error", "Try again with a better view of the sky.", PRIVATE);
  }
  else {
    Particle.publish("error", "Hey, it worked!", PRIVATE);
  }

  return 0;
}
