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

Fibre can be compiled for all kinds of platforms and use all kinds of transport providers. But to make life simple, `libfibre` already has built-in support for a couple of platforms and transport layers:

|     | Windows      | macOS [1]    | Linux        | Web [2]      |
|-----|--------------|--------------|--------------|--------------|
| USB | yes (libusb) | yes (libusb) | yes (libusb) | yes (WebUSB) |

 - [1] macOS 10.9 (Mavericks) or later
 - [2] see [Fibre-JS](js/README.md)

## Implementations

 * **C/C++**: See [fibre-cpp](cpp/README.md).
 * **Python**: See [PyFibre](python/README.md).
 * **JavaScript**: See [Fibre-JS](js/README.md).

Under the hood all language-specific implementations just bind to the C/C++ implementation which we provide as a precompiled library `libfibre`.

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
