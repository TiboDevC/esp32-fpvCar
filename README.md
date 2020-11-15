# ESP31-CAM with OV2640 as FPV car

This project is a first person view car (FPV) with real time with 
custom car and PCB.

Based on existing git projects like:
- https://github.com/arkhipenko/esp32-mjpeg-multiclient-espcam-drivers
- https://github.com/CoretechR/ESP32-WiFi-Robot

## Status
### Electronic
PCB design still under development.

### Mechanic
The frame is finished. The rest will come when the electronics are finalized.

### Software
ESP32 is able to stream the video via websocket up to 10 clients.
This is a first draft, resource management is not optimal so the ping is ok 
but jitter is too high. 
 