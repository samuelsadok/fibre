
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
        'usb_discoverer.cpp'
    }, -- 'posix_udp.cpp'},
    libs={'pthread', 'usb-1.0', 'udev'},
    headers={'include', '/usr/include/libusb-1.0'} -- todo: pkg-config libusb --cflags
}
