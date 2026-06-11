# Mood Paintings

A generative art system that creates real-time visual paintings based on your heartbeat.

## How it works
A MAX30102 heart rate sensor reads biometric data from the user and transmits it 
over WiFi via UDP to a Processing sketch, which generates dynamic visual art 
that responds to heart rate variability (HRV) in real time.

## Tech stack
- Arduino IDE (MAX30102 sensor, WiFi UDP transmission)
- Processing (generative art visualisation)
- HRV/RMSSD algorithm for biometric analysis
- Personal baseline calibration system

## Files
- `mood_sensor.ino` — Arduino code for heart rate sensing and WiFi transmission
- `mood_paintings.pde` — Processing sketch for real-time generative art

## Built for
Monash University FIT3146 Maker Lab — presented at end-of-semester Expo
