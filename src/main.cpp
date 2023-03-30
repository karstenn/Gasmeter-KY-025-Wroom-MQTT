/*
This script does:
- use the KY-025 reed to detect if there is an increment (magnetic field) at the physical gasmeter
- use NTP to setup the initial rtc time at startup
- use mqtt to send the status (see below)

I found out that the KY-025 does not provide a stable output signal. Contrary to the documentation on the Internet, it actually "bounces". 
This means that short interruptions occur when reading out the magnetic field, which inevitably lead to measurement errors. For example, if you count the change from LOW to HIGH, the KY-025 would send much more frequent counts than there actually are (or less).

These dropouts may be so short that the onboard led does not show them. However, if you look closely at the values (analogue and digital) in the terminal, you can detect these dropouts. 

Let's assume we detect a signal as soon as the digit "7", for example, is visible in the meter window. In my case, I can then read this field above the second decimal place. 
Then the KY-025 detects a HIGH signal (with the above-mentioned dropouts). 
Depending on the gas flow, this phase lasts a certain time. Let's say about 20 seconds at full consumption. When the counter changes, i.e. the digit 7 disappears, the stomach field also drops. 
The KY-025 then no longer detects a magnetic field and the digital output is LOW. The low period is usually very long, while the high period is shorter (but still significant).

To eliminate the bouncing of the KY-025, I use two counter variables. 
In a certain time, I increase the counters until a certain maximum value is reached, and I do this whenever I can measure a valid signal (high or low).
So I regularly read out the digital pin (according to a time specification: sending_mqtt_every_ms) and increment my counters to the maximum (boundryValHigh or boundryValLow) when a valid signal is present. 

Within these limits, I can safely assume that a stable signal has been present for a certain time. 
If this is the case, I send my messages to the MQTT broker. 

In Home Assistant, for example, I can then watch for the change of state and count up a Home Asisstant-internal counter that monitors the gas consumption.


MQTT:
The current status of reading will be sent by two messgages (only one of booth can the TRUE):
/gasmeter-ky025/highDetected=TRUE/FALSE
/gasmeter-ky025/lowDetected=TRUE/FALSE

Booth are sent in sync with a timestamp, e.g:
lastHigh=Sunday, March 26 2023 14:43:22
lastLow=Sunday, March 26 2023 14:43:27

Thus, in homeassistant you can track the change of status, e.g. every time highDetected was set to TRUE (or FALSE)
*/


#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP32Time.h>


ESP32Time rtc;


int ledPin = 13; //19
int digitalPin = 26; // KY-025 digital interface
int analogPin = 32; // KY-025 analog interface (AD2 is being used for WIFI)


int digitalVal; // digital readings
int analogVal; //analog readings


int cntHigh=0; // increment counter to detect a stable high signal
int cntLow=0; // increment counter to detect a stable low signal
int boundryValHigh=4; // once there have been more than x high signals being deteted, "wasHigh" will be set to true (be aware! there is a delay of 'sending_mqtt_every_ms' = 5000)
int boundryValLow=8; // once there have been more than x low signals being deteted, the MQTT signal will be sent and cntHigh will be reseted (be aware! there is a delay of 'sending_mqtt_every_ms' = 5000)
bool wasHigh=false; // set if there has been a stable high signal
bool mqttWasSent=false;

// WiFi-Credentials
const char *ssid = "op://Development/WIFI_HOME/Section_aclahbwfk375cnbmx25dbhaxgu/SSID"; // add your wifi SSID here
const char *password = "op://Development/WIFI_HOME/Section_aclahbwfk375cnbmx25dbhaxgu/passwort"; // add your wifi password here

// MQTT Broker IP address
const char *mqtt_identifier = "gasmeter-ky025"; // chose your name for the device
const char *mqtt_server = "op://Development/MQTT_HOME/add more/URL"; // add your server IP here
const char *mqtt_user = "op://Development/MQTT_HOME/add more/user"; // add your mqtt user name here
const char *mqtt_pass = "op://Development/MQTT_HOME/add more/Passwort"; // add your mqtt password here

int sending_mqtt_every_ms = 5000;

WiFiClient espClient;
PubSubClient client(espClient);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
//String formattedDate;
//String dayStamp;
//String timeStamp;

// general variables
long lastMsg = 0;
char msg[50];
char topic[50];
int value = 0;

float temperature = 0;
float humidity = 0;





void setup_wifi()
{
    delay(10);
    // We start by connecting to a WiFi network
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void callback(char *topic, byte *message, unsigned int length)
{
    Serial.print("Message arrived on topic: ");
    Serial.print(topic);
    Serial.print(". Message: ");
    String messageTemp;

    for (int i = 0; i < length; i++)
    {
        Serial.print((char)message[i]);
        messageTemp += (char)message[i];
    }
    Serial.println();

    // Feel free to add more if statements to control more GPIOs with MQTT

    // If a message is received on the topic esp32/output, you check if the message is either "on" or "off".
    // Changes the output state according to the message
    strcpy(topic, mqtt_identifier);
    strcat(topic, "/output");
    if (String(topic) == topic)
    {
        Serial.print("Changing output to ");
        if (messageTemp == "ON")
        {
            Serial.println("on");
            digitalWrite(ledPin, HIGH);
        }
        else if (messageTemp == "OFF")
        {
            Serial.println("off");
            digitalWrite(ledPin, LOW);
        }
    }
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect(mqtt_identifier, mqtt_user, mqtt_pass))
        {
            Serial.println("connected");
            // Subscribe
            strcpy(topic, mqtt_identifier);
            strcat(topic, "/output");
            client.subscribe(topic);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void setup(void)
{
    // PIO delay (needed to see the debug messages)
    delay(2500);

    // initialize the basics
    Serial.begin(115200);
    Serial.println("Start");
    pinMode(ledPin, OUTPUT);

    // set pins for the KY-025
    pinMode(analogPin, INPUT);
    pinMode(digitalPin, INPUT);
    
     // setup I2C
    Wire.begin(21, 22);
   
    // setup WIFI
    setup_wifi();
    
    // initialize MQTT and set callback
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    // Initialize a NTPClient to get time
    timeClient.begin();
    // Set offset time in seconds to adjust for your timezone, for example:
    // GMT +1 = 3600; GMT +8 = 28800; GMT -1 = -3600; GMT 0 = 0
    timeClient.setTimeOffset(3600); // Berlin

   // setup RTC
    // get & sent timestamp
    Serial.print("NTP getting time: ");
    while(!timeClient.update()) {
        Serial.print(".");
        timeClient.forceUpdate();
        delay(100);
    }
    Serial.println("Done.");

    // The formatted Date comes with the following format: 2018-05-28T16:00:13Z
    String formattedDate = timeClient.getFormattedTime();
    rtc.setTime( timeClient.getSeconds(), timeClient.getMinutes() , timeClient.getHours() ,  26 , 3 , 2023);  // 17th Jan 2021 15:24:30

    formattedDate = rtc.getDateTime(true); // rtc.getTime("%A, %B %d %Y %H:%M:%S")
    Serial.println(formattedDate);

}

// funtion to send (toggle) status of signal detectiomn and to keep track of the last toime of occurence
// booth, status and timestamp are sent to MQTT
void sentDetectionState( boolean state ){

    String formattedDate = rtc.getDateTime(true); // rtc.getTime("%A, %B %d %Y %H:%M:%S")


    Serial.println(formattedDate);
   
    // convert string to char
    int str_len = formattedDate.length() + 1; 
    char char_array[str_len];
    formattedDate.toCharArray(char_array, str_len);


    // toogle according to state
    if( state ){
        // timestamp
        strcpy(topic, mqtt_identifier);
        strcat(topic, "/lastHigh" );
        client.publish(topic, char_array );

        // sent highDetected=TRUE
        strcpy(topic, mqtt_identifier);
        strcat(topic, "/highDetected");
        client.publish(topic, "TRUE");

        // sent lowDetected=FALSE
        strcpy(topic, mqtt_identifier);
        strcat(topic, "/lowDetected");
        client.publish(topic, "FALSE");

    } else {
        // timestamp
        strcpy(topic, mqtt_identifier);
        strcat(topic, "/lastlow" );
        client.publish(topic, char_array );

        // sent lowDetected=TRUE
        strcpy(topic, mqtt_identifier);
        strcat(topic, "/lowDetected");
        client.publish(topic, "TRUE");

        // sent highDetected=FASLE
        strcpy(topic, mqtt_identifier);
        strcat(topic, "/highDetected");
        client.publish(topic, "FALSE");
    }

}


void loop()
{
    // verify MQTT connection
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    long now = millis();
    if (now - lastMsg > sending_mqtt_every_ms)
    {
        lastMsg = now;

        // check if mqtt messages have been sent before
        if( mqttWasSent ){
            // toggle detection state (here: false)
            sentDetectionState( false );
            mqttWasSent = false;
        }
   

        // Read the digital interface
        digitalVal = digitalRead(digitalPin); 
        //analogVal = analogRead(analogPin); 
        
        
        // check if there isnt/is a signal (magnetic field)
        if(digitalVal == LOW)
        {
            // LOW signal detected
            cntLow++; 
            Serial.print("cntLOW: "); 
            Serial.println(cntLow);   
        }
        else 
        {
            // HIGH detected
            Serial.print("cntHIGH: ");   
            Serial.println(cntHigh);   

            if( cntHigh++ >= boundryValHigh ){
                // sum until maximum is reached; thus, stable high can be assumed
                Serial.println("Stable HIGH signal detected (cntHigh++ >= boundryValHigh) -> cntLow=0"); 
                wasHigh = true;
                cntLow = 0; // reset low counter
            }

        }
        
        // check if there have been detected "enough" (defined by boundryValLow) LOW signals
        if(cntLow >= boundryValLow && wasHigh){
            if( cntHigh >= boundryValHigh){

                Serial.println("After stable HIGH signal has been detected, now stable low has been detected, too: Send MQTT + Reset cntHigh"); 

                // toggle detection state (here: HIGH)
                sentDetectionState( true );

                // example "template to convert a value into char"
                // Convert the value to a char array
                /*
                char tempString[16];
                dtostrf(digitalVal, 1, 2, tempString);
                Serial.print("digitalVal: ");
                Serial.println(tempString);
                strcpy(topic, mqtt_identifier);
                strcat(topic, "/digitalVal");
                client.publish(topic, tempString);
                
                Serial.print("MQTT-Publish: ");
                Serial.println(tempString);
                */



                // reset all counters & logic tracker
                cntHigh=0;
                cntLow=0;
                wasHigh = false;


                // keep track of, that MQTT has been sent
                mqttWasSent = true;

            }
        }
        
    }
}