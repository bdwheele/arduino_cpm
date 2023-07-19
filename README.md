# arduino_cpm
Build a real-ish CP/M machine using arduinos, floppy drive, etc.

Build something that looks like a real CP/M machine:
* CPU/RAM via RunCPM on either a Pico or an ESP32
* floppy drive(s) via https://github.com/dhansel/ArduinoFDC using a leonardo
  * read/write 360K or 1.44M floppies
  * 512 byte sectors in IBM format
* serial console  
* Future things:
  * hard disk via SD card?
  * additional serial via tcp/ip?
