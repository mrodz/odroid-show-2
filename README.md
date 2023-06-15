# This repository saves my progress writing firmware for the Odroid Show 2.

Unfortunately, my time was cut short because of an electronics mishap. Here is a log of my work:

- 6/12/2023: Attempted to write universal drivers using [libusb](https://libusb.info/) to interface with the shipped firmware on the device.
- 6/13/2023: Attempts in vain; the firmware communicates over `COM` ports, and Windows does not recognize the Odroid Show 2 as a `COM` listener/writer.
- 6/14/2023 (Morning): Switched gears to writing new firmware for the **extremely** memory-constrained (2k bytes) device. 
  Set up the groundwork for applications, window contexts, and graphics.
- 6/14/2023 (Afternoon): During one deployment, the device turned off unexpectedly. The Show 2 was still recognized as a USB device. However, the bootloader and
  programmer had been corrupted (possibly because of an unsafe pointer operation?). 
- 6/14/2023 (Night): All hands on deck to resuscitate the device! I used the SPI interface to burn the bootloader back onto the device, but...

## End of life.
The device was recieving power via a USB-UART bridge connected to my computer, which had been giving inconsistent power. [AvrDude](https://github.com/avrdudes/avrdude) 
succesfully burned the bootloader, but sometime after this the Odroid recieved a power surge and one of the transistors failed. The region of the chip close to the
USB-UART processor reached temperatures over 102 degrees and the device would not boot.
