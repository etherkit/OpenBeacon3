OpenBeacon 2
============
500 mW Si5351C-based MEPT Transmitter

Display
-------
Line 1: GPS status (internal or external antenna, number of satellites fixed), UTC time, transmission mode

Line 2: Frequency

Line 3: PA current, input voltage

Line 4: Message buffer contents

Line 5: Button functions

LEDs
----
* **D1** (red)

    Transmit<br />
    UART TX (during firmware loading)  

* **D2** (green)

    UART RX (during firmware loading)
    
* **D3** (blue)

    PA enabled

* **D5** (yellow)

    Power

User Interface
--------------
* **Tuning**

    Turn S4 rotary encoder to tune
    
* **Tuning Rate**

    Depress rotary encoder shaft momentarily to step through tuning rates. An underline cursor will indicate which frequency digit is being changed.
    
* **Change Band or Mode**

    Hold rotary encoder shaft in for 1 second to access the band and mode change menu. Turn the rotary encoder to cycle between selections. Press button S1 to change the band selection or press button S2 to change the mode selection. Press button S3 to exit this menu.

* **Configuration Menu (S1)**

    Not yet implemented
    
* **Start/Stop (S2)**

    Press button to immediately start a transmission if the transmitter is currently idle, or immediately stop a transmission if one is currently active.
    
* **TX Enb/TX Dis (S3)**

    Press button to toggle the state of recurring transmissions.


Message Buffer System
---------------------
When using the CW, QRSS, DFCW, and Hell modes, five different message buffers are available for transmission. Message Buffer 0 is a special case and consists solely of the value set in the ```callsign``` configuration variable. Message Buffers 1 through 4 can be set to any 40 character (consisting of letters, numbers, or characters ```/```, ```=```, and ```?```) user message via the configuration system. OpenBeacon 2 can be set transmit the contents of any of the five message buffers during a transmit period.

In WSPR mode, OpenBeacon 2 uses the callsign specified in the ```callsign``` configuration variable, the current Maidenhead grid square as determined from GPS (or the default value placed in the ```grid``` configuration variable if the GPS does not have a location fix), and the power level specified in the  ```dbm``` configuration variable.

Configuration
-------------
The configuration for OpenBeacon 2 is loaded via the included USB-UART bridge. To send configuration data, send OpenBeacon 2 a string consisting of the character 'W', followed by a JSON string setting the desired conifiguration variables, followed by a '@' termination character. For example:

    W{"callsign":"W1AW","default_buffer":2,"msg_mem_2":"HELLO WORLD"}@
    
Here is the list of available configuration variables:

* **default_buffer**
* **msg_mem_1**
* **msg_mem_2**
* **msg_mem_3**
* **msg_mem_4**
* **callsign**
* **grid**
* **dbm**
* **ext_gps_ant**
* **ext_pll_ref**
* **ext_pll_ref_freq**

Required Libraries
------------------
Install these library from the indicated locations before attempting to compile and upload your own firmware.

* Flash Storage [https://github.com/cmaglie/FlashStorage](https://github.com/cmaglie/FlashStorage)
* TinyGPS++ [http://arduiniana.org/libraries/tinygpsplus/](http://arduiniana.org/libraries/tinygpsplus/)
* Etherkit SSD1306 [https://github.com/etherkit/SSD1306-Arduino](https://github.com/etherkit/SSD1306-Arduino)
* Etherkit Si5351 (Library Manager)
* Etherkit JTEncode (Library Manager)
* Scheduler (Library Manager)
* ArduinoJson (Library Manager)
* SPI (Arduino Standard Library)
* Wire (Arduino Standard Library)

Updating the Firmware
---------------------
Connect OpenBeacon 2 to your PC via USB. In the Arduino IDE, select board type ```Arduino/Genuino Zero (Native USB Port)``` and the correct virtual serial port. Sketches can be uploaded to OpenBeacon 2 as if it were a standard Arduino Zero (although a custom hardware configuration is coming soon).

TODO
----
  - [ ] Internal/External Ref Osc
  - [ ] Internal/External GPS Antenna
  - [ ] Get Hell mode working
