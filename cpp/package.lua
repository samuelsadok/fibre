
fibre_dir = tup.getcwd()

function fibre_package(platform)
    local pkg = {
        root = fibre_dir,
        sources = {
            --'fibre.cpp',
            'calls.cpp',
            'local_endpoint.cpp',
            'stream.cpp',
            'dispatcher.cpp',
            --'output.cpp',
            --'remote_node.cpp',
            'logging.cpp',
            'usb_discoverer.cpp',
            'udp_discoverer.cpp',
            --'shared_memory_discoverer.cpp',
            'platform_support/bluez.cpp',
            'platform_support/dbus.cpp',
            'platform_support/posix_udp.cpp',
            'platform_support/posix_tcp.cpp',
            'platform_support/posix_socket.cpp',
            'platform_support/linux_event.cpp',
            'platform_support/linux_timer.cpp',
            'platform_support/linux_worker.cpp',
        },
        extra_inputs = {'platform_support/dbus_interfaces/*.hpp'},
        include_dirs = {'include'},
        depends = {'pthread'},
        outputs = {
            exported_includes = {'include'}, -- todo: consistent naming
            exported_extra_inputs = {'platform_support/dbus_interfaces/*.hpp'}
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
