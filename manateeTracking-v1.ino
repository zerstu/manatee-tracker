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

const unsigned long PUBLISH_PERIOD = 5 * 60 * 1000; // minutes * seconds * ms
const unsigned long SLEEP_TIME_SECS = 2 * 60 * 60; // hours * minutes * seconds

// TinyGPS++ object declaration
TinyGPSPlus gps;
unsigned long lastPublish = 0;

// FuelGauge declaration for 'batt' function
FuelGauge fuel;

#define AssetTrackerSerial Serial1

#define IridiumSerial Serial4
IridiumSBD modem(IridiumSerial, D0);

void setup()
{
    Serial.begin(9600);

    AssetTrackerSerial.begin(9600);
    IridiumSerial.begin(19200);
    modem.sleep();

    // Power up the GPS module
    // Setting D6 LOW begins the Asset Tracker (t.gpsOn)
    pinMode(D6, OUTPUT);
    digitalWrite(D6, LOW);

    Particle.function("gps", gpsPublish);
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
      // This if statement ensures that enough time has passed for the GPS to get a fix
      if (millis() - lastPublish >= PUBLISH_PERIOD) {
        // Store the time when we last published a coordinate
        lastPublish = millis();
        // Declare a buffer in order to store the message (coordinate or failed attempt) that is to be sent
        char buf[128];

        // Make sure that the location obtained by the GPS is valid
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
        // If the Particle is connected, we also publish the battery info (voltage and state of charge)
        if (Particle.connected()) {
            Particle.publish("G", buf, PRIVATE);
            Particle.publish("B", "v:" + String::format("%.2f", fuel.getVCell())
                + ", c:" + String::format("%.2f", fuel.getSoC()),
                60, PRIVATE);
        }
        else {
            // Start the modem (which has been asleep this entire time to conserve battery)
            modem.begin();

            // Send the message
            // Afterwards, we put the modem back to sleep to conserve battery
            modem.sendSBDText(buf);
            modem.sleep();
        }

        // The delay here ensures that by publishing, we don't accidentally ping cellular
        // This would immediately wake up our Electron after putting it into stop mode

        delay(10000);

        // This cellular command allows us to wake the Electron through cellular

        Cellular.command("AT+URING=1\r\n");
        delay(1000);

        // Set the Electron to stop mode but still listening
        // Only drains about 4.5 mA in this stop mode

        System.sleep(RI_UC, RISING, SLEEP_TIME_SECS, SLEEP_NETWORK_STANDBY);
        Cellular.command("AT+URING=0\r\n");
      }
}

// Function to ping the Manatee Tracker
// This will restart the 2 hour sleep timer, meaning that after you ping the device, another coordinate will be published in 5 minutes
// Then the device will go to sleep again for 2 hours
// Behaves very similarly to displayInfo()
// The function returns 0 if no location/GPS fix is obtained
// The function returns 1 if the GPS coordinate was transmitted to the Particle cloud
// The function returns 2 if the GPS coordinate was transmitted to the Iridium satellite network
int gpsPublish(String command)
{
    while (AssetTrackerSerial.available() > 0)
        gps.encode(AssetTrackerSerial.read());

    char buf[128];
    if (gps.location.isValid()) {
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
