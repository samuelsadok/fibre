
tup.include('../tupfiles/build.lua') -- import build system functions
tup.include('../cpp/package.lua') -- import fibre package

packages = {
    make_exe_package({
        sources = {'cpp_utils_test.cpp'},
        include_dirs = {'../cpp/include'}
    }),
    make_exe_package({
        sources = {'codec_test.cpp', '../cpp/logging.cpp'},
        include_dirs = {'../cpp/include'}
    }),
    make_exe_package({
        sources = {'defragmenter_test.cpp', '../cpp/logging.cpp', '../cpp/stream.cpp'},
        include_dirs = {'../cpp/include'}
    }),
    make_exe_package({
        sources = {'worker_test.cpp'},
        depends = {'fibre'}
    }),
    make_exe_package({
        sources = {'usb_test.cpp'},
        depends = {'fibre'}
    }),
    make_exe_package({
        sources = {'dbus_test.cpp'},
        depends = {'fibre', 'libdbus'}
    }),
    make_exe_package({
        sources = {'bluetooth_test.cpp'},
        depends = {'fibre', 'libdbus'}
    }),
    make_exe_package({
        sources = {'udp_test.cpp'},
        depends = {'fibre'}
    }),
    make_exe_package({
        sources = {'call_decoder_test.cpp'},
        depends = {'fibre'}
    }),
    --make_exe_package({
    --    sources = {'run_tests.cpp'},
    --    depends = {'fibre'}
    --})
}

tup.foreach_rule(
    {"../tools/dbus_interface_definitions/*.xml"},
    "python ../tools/dbus_interface_parser.py -d %f -t ../tools/dbus_interface.c.j2 -o %o",
    {"../cpp/dbus_interfaces/%B.hpp"}
)

--toolchain=GCCToolchain('', 'build', {'-O3', '-fvisibility=hidden', '-frename-registers', '-funroll-loops'}, {})
--toolchain=GCCToolchain('', 'build', {'-O3', '-g', '-Wall'}, {})
--toolchain=GCCToolchain('avr-', {'-Ofast', '-fvisibility=hidden', '-frename-registers', '-funroll-loops', '-I/home/samuel/stlport-avr/stlport'}, {})
--toolchain=LLVMToolchain('x86_64', {'-O3', '-fno-sanitize=safe-stack', '-fno-stack-protector'}, {'-flto', '-Wl,-s'})
--toolchain=LLVMToolchain('avr', {'-O3', '-std=gnu++11', '--target=avr', '-fno-sanitize=safe-stack', '-fno-stack-protector', '-I/home/samuel/stlport-avr/stlport'}, {'-flto', '-Wl,-s'})

potential_platforms = {
    '',
    --'arm-none-eabi', std::mutex not supported
    --'arm-linux-gnueabi', -- compiler not installed
    --'arm-linux-gnueabihf', -- Raspberry Pi (armv7l)
    --'mipsel-linux-gnu', -- MIPS (missing the STL on my installation, std::tuple not supported)
    --'avr', -- Atmel/Microchip AVR (std::vector not supported)
    --'i686-w64-mingw32', -- Windows 32bit (eventfd not found)
    --'x86_64-w64-mingw32' -- Windows 64bit (eventfd not found)
}

for _, platform_name in ipairs(potential_platforms) do
    platform = make_platform(platform_name)
    build_packages(packages, {}, platform)
end


-- try to compile a few components for windows (will be removed when everything compiles)
winpkg1 = make_exe_package({
    sources = {'worker_test.cpp', '../cpp/windows_worker.cpp', '../cpp/logging.cpp'},
    include_dirs = {'../cpp/include'},
    outputs = {exported_lib_flags = {'-lws2_32'}}
})
winplatform = make_platform('x86_64-w64-mingw32')
build_packages({winpkg1}, {}, winplatform)

winpkg2 = make_exe_package({
    sources = {'udp_test.cpp', '../cpp/windows_worker.cpp', '../cpp/logging.cpp', '../cpp/windows_udp.cpp', '../cpp/windows_socket.cpp'},
    include_dirs = {'../cpp/include'},
    outputs = {exported_lib_flags = {'-lws2_32'}}
})
winplatform = make_platform('x86_64-w64-mingw32')
build_packages({winpkg2}, {}, winplatform)
