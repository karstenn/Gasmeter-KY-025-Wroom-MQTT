# Gasmeter-KY-025-Wroom-MQTT

This script does use:

- the KY-025 reed to detect if there is an increment (magnetic field) at the physical gasmeter

- NTP to setup the initial rtc time at startup

- mqtt to send the status (see below)


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
