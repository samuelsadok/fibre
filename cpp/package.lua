
fibre_dir = tup.getcwd()

function fibre_package(platform)
    local pkg = {
        root = fibre_dir,
        sources = {
            --'fibre.cpp',
            'input.cpp',
            --'output.cpp',
            --'remote_node.cpp',
            --'posix_tcp.cpp',
            'logging.cpp',
            'worker.cpp',
            'timer.cpp',
            'signal.cpp',
            'usb_discoverer.cpp',
            'bluetooth_discoverer.cpp',
            --'shared_memory_discoverer.cpp',
            --'udp_transport.cpp',
            'dbus.cpp'
        },
        extra_inputs = {'dbus_interfaces/*.hpp'},
        include_dirs = {'include'},
        depends = {'pthread'},
        outputs = {
            exported_includes = {'include'}, -- todo: consistent naming
            exported_extra_inputs = {'dbus_interfaces/*.hpp'}
        }
    }

    -- If WinUSB is available, we are probably on Windows and there we prefer
    -- WinUSB over libusb because WinUSB.sys can be loaded automatically based
    -- on the USB device descriptors.
    if try_build_package('winusb', platform) then
        pkg.optional_depends += 'winusb'
    else
        pkg.optional_depends += 'libusb'
    end

    pkg.optional_depends += 'libdbus'
    pkg.optional_depends += 'libudev'

    return make_obj_package(pkg)(platform)
end

register_package('fibre', fibre_package)
