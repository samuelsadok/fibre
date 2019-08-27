
tup.include('../tupfiles/build.lua')

fibre_package = define_package{
    sources={
        'cpp_utils_tests.cpp',
        'fibre.cpp',
        'input.cpp',
        'output.cpp',
        'remote_node.cpp',
        'posix_tcp.cpp',
        'worker.cpp',
        'timer.cpp',
        'signal.cpp',
        'usb_discoverer.cpp',
        'bluetooth_discoverer.cpp',
        'dbus.cpp'
    }, -- 'posix_udp.cpp'},
    libs={'pthread', 'usb-1.0', 'udev', 'dbus-1'},
    headers={'include',
        '/usr/include/libusb-1.0', -- todo: pkg-config libusb --cflags
        '/usr/include/dbus-1.0', '/usr/lib/dbus-1.0/include' -- todo: pkg-config dbus-1 --cflags
    }
}
