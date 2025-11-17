# Silabs SimpleFOC motor control with BLE example

This repository contains the example application to demonstrate the simpleFOC closed loop speed control operation with BLE communication. The application accepts BLE connection from a mobile phone and receives commands via Serial Profile Protocol (SSP). The serial protocol is same what simpleFOC is using via the wired serial interface.

## Hardware

Currently two Arduino boards are supported:

 - *Arduino Nano Matter* (this is part of the Motor Control Demo Kit)
 - *SparkFun SparkFun Thing Plus Matter*
 
The I/O configuration is automatically set tho the used board at build time.

## Build

To properly build the application the next Arduino software environment is required:

 - *Silabs Arduino Core* v3.0.0 [download link](https://github.com/SiliconLabs/arduino/releases/tag/3.0.0)
 - *simpleFOC Arduino library* v2.3.6 (until the official v2.3.6 is not released you have to clone the development branch) [download link](https://github.com/simplefoc/Arduino-FOC/tree/dev)
 

 
 

