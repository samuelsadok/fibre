
## Overview ##

`lightd` runs as a service on your Raspberry Pi and lets you control the connected LED strips based on commands it receives from the network. There's a simple python script you can run anywhere on the same network.

The LED control is based on [rpi_ws281x](https://github.com/jgarff/rpi_ws281x). The supported LEDs include any RGB and RGBW LED strips that speak the protocol of the WS281x or SK6812 LEDs. The supported interfaces are PWM, PCM and SPI.

The network connectivity is based on [Fibre](https://github.com/samuelsadok/fibre). In the future this means easy zero-config control from most OSses using your language of choice.

Note that this project is in an alpha stage and while basic control is working it will undergo breaking changes.

## Quick Start Guide ##

### Configuration ###
 - Adapt the LED-driver configuration in `lightd.cpp`, around lines 12 to 43.
 - Set the correct IP address or hostname in `lightctl.py`

### Compilation ###
First, you need a compiler (obviously). If you're cross-compiling from your standard x86 PC for the Raspberry Pi, install the `arm-linux-gnueabihf-gcc` toolchain. Otherwise you can compile directly on the RPi using its native compiler. Either way, make sure the value of `TOOLCHAIN` in `Tupfile.lua` is correct.
Next you need the `tup` build system (why another niche build system? because this one is logically sound, that's a big plus). Now just run `tup init` and `tup` in the top level directory of the repo.

### Installation ###
On your Raspberry Pi (or whatever you connect the LEDs to):

    sudo ./install.sh
    sudo systemctl enable lightd
    sudo systemctl start lightd
    sudo systemctl enable lights-off.timer
    sudo systemctl start lights-off.timer

The last two commands will also make your lights turn red and then off every day at midnight.

### Usage ###

    `lightctl.py`
