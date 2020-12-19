# Fibre

## Overview

The goal of Fibre is to provide a framework to make suckless distributed applications easier to program.

In particular:

 - Nobody likes boiler plate code. Using a remote object should feel almost
   exactly as if it was local. No matter if it's in a different process, on
   a USB device, connected via Bluetooth, over the Internet or all at the
   same time.
   All complexity arising from the system being distributed should be taken
   care of by Fibre while still allowing the application developer to easily
   fine-tune things.

 - Fibre has the ambition to run on most major platforms and provide bindings
   for the most popular languages. Even bare metal embedded systems with very
   limited resources. See the Compatibility section for the current status.

 - Once you deployed your application and want to change the interface, don't
   worry about breaking other applications. With Fibre's object model, the
   most common updates like adding methods, properties or arguments won't
   break anything. Sometimes you can get away with removing methods if they
   weren't used by other programs. **This is not implemented yet.**

## Platform Compatibility

Fibre can be compiled for many of platforms and work on many kinds of transport layers. But to make life simple Fibre already ships with built-in support for a couple of backends which can be enabled/disabled selectively. The [official precompiled binaries](https://github.com/samuelsadok/fibre/releases) (and by extension all language bindings) have all available backends enabled. These backends are available:

|                           | Windows      | macOS [1]    | Linux        | Web [2]      |
|---------------------------|--------------|--------------|--------------|--------------|
| USB (`usb`)               | yes (libusb) | yes (libusb) | yes (libusb) | yes (WebUSB) |
| TCP client (`tcp-server`) | no           | no           | yes          | no           |
| TCP server (`tcp-client`) | no           | no           | yes          | no           |

 - [1] macOS 10.9 (Mavericks) or later
 - [2] see [fibre-js](js/README.md)

## Channel Specs

When discovering objects and publishing objects, the caller usually specifies which backends to discover/publish on. This is specified through a channel spec string.

The channel spec string has the form `backend1:key1=val1,key2=val2;backend2:key1=val1,key2=val2;backend3`. The following sections describe the available backends and the arguments they support. Integers can be in decimal as well as hexadecimal notation (`0x1234`).

### `usb`

**Compile option:** `FIBRE_ENABLE_LIBUSB_BACKEND`

**Parameters:**

 - `bus` (int): Only accept devices on this bus number.
 - `address` (int): Only accept the USB device with this device address. The device address usually changes when the device is replugged.
 - `idVendor` (int): Only accept devices with this Vendor ID.
 - `idProduct` (int): Only accept devices with this Product ID.
 - `bInterfaceClass` (int): The interface class of the compatible interface or interface association.
 - `bInterfaceSubClass` (int): The interface subclass of the compatible interface or interface association.
 - `bInterfaceProtocol` (int): The protocol of the compatible interface or interface association.

Omitted parameters are ignored during filtering.

**Example:** `usb:idVendor=0x1209,idVendor=0x0d32` looks for channels on USB devices with VID:PID 1209:0d32.

### `tcp-client`

 - `address` (string): The IP address or hostname of the remote server to connect to.
 - `port` (int): The port on which to connect.

### `tcp-server`

 - `address` (string): The IP address of the local server on which to listen.
 - `port` (int): The port on which to listen.

### `serial`

 - `path` (int): The name or path of the serial port. On Unix systems this is usually something like `/dev/ttyACM0` and on Windows something like `COM1`.

**Example:** `serial:path=/dev/ttyACM0` looks for channels on the serial port /dev/ttyACM0.

## Implementations

 * **C++**: See [fibre-cpp](cpp/README.md).
 * **C**: See [fibre-cpp](cpp/README.md), specifically `libfibre.h`.
 * **Python**: See [PyFibre](python/README.md).
 * **JavaScript**: See [fibre-js](js/README.md).

Under the hood all language-specific implementations bind to the C++ implementation which we provide as a [precompiled library](https://github.com/samuelsadok/fibre/releases) `libfibre`.

## Adding Fibre to your project

We recommend Git subtrees if you want to include the Fibre source code in another project.
Other contributors don't need to know anything about subtrees. To them the Fibre repo will be like any other normal directory.

#### Adding the repo
```
git remote add fibre-origin git@github.com:samuelsadok/fibre.git
git fetch fibre-origin
git subtree add --prefix=fibre --squash fibre-origin master
```

If you only need support for a specific programming language you can also just include the language-specific repository Instead of the whole main repository.

#### Pulling updates from upstream
```
git subtree pull --prefix=fibre --squash fibre-origin master
```

#### Contributing changes back to upstream
This requires push access to `fibre-origin`.
```
git subtree push --prefix=fibre fibre-origin master
```

## Projects using Fibre ##

 - [ODrive](https://github.com/madcowswe/ODrive): High performance motor control
 - [lightd](https://github.com/samuelsadok/lightd): Service that can be run on a Raspberry Pi (or similar) to control RGB LED strips

## Contribute ##

This project losely adheres to the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).

## Credits ##

A significant portion of the code in this repository was written for and financed by [ODrive Robotics Inc](https://odriverobotics.com/).
