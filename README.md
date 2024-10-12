# omdazz-cyclone-iv-raspberry-pi-flash

This project explains how to flash Cyclone IV FPGA on OMDAZZ development board using GPIO of Raspberry Pi without usb-blaster (or any other device) using [https://sourceforge.net/projects/urjtag/](urjtag) tool.
The code is a modified version of the [https://sourceforge.net/projects/urjtag/](urjtag).

# Why

The first problem of Raspberry Pi flashing the Cyclone IV FPGA is the fact that the Quartus is not supported for the ARM devices (like Raspberry Pi).
There's an open-source tool called [https://sourceforge.net/projects/urjtag/](urjtag), but unfortunately it works with an old sysfs GPIO interface which is now [https://www.thegoodpenguin.co.uk/blog/stop-using-sys-class-gpio-its-deprecated/](deprecated) and is no longer used.
So, in order to be able to flash the FGPA from Raspberry Pi, you can use the code provided in this repository and override the
