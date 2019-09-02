
tup.include('../tupfiles/build.lua') -- import build system functions
tup.include('../cpp/package.lua') -- import fibre package


usb_test = make_exe_package({
    sources = {'usb_test.cpp'},
    depends = {'fibre'}
})

dbus_test = make_exe_package({
    sources = {'dbus_test.cpp'},
    depends = {'fibre', 'libdbus'}
})

unit_tests = make_exe_package({
    sources = {'run_tests.cpp'},
    depends = {'fibre'}
})


--toolchain=GCCToolchain('', 'build', {'-O3', '-fvisibility=hidden', '-frename-registers', '-funroll-loops'}, {})
--toolchain=GCCToolchain('', 'build', {'-O3', '-g', '-Wall'}, {})
--toolchain=GCCToolchain('avr-', {'-Ofast', '-fvisibility=hidden', '-frename-registers', '-funroll-loops', '-I/home/samuel/stlport-avr/stlport'}, {})
--toolchain=LLVMToolchain('x86_64', {'-O3', '-fno-sanitize=safe-stack', '-fno-stack-protector'}, {'-flto', '-Wl,-s'})
--toolchain=LLVMToolchain('avr', {'-O3', '-std=gnu++11', '--target=avr', '-fno-sanitize=safe-stack', '-fno-stack-protector', '-I/home/samuel/stlport-avr/stlport'}, {'-flto', '-Wl,-s'})

potential_platforms = {
    '',
    --'arm-none-eabi', std::mutex not supported
    --'arm-linux-gnueabi', -- compiler not installed
    --'arm-linux-gnueabihf', -- Raspberry Pi (armv7l) (static assert fails)
    --'mipsel-linux-gnu', -- MIPS (missing the STL on my installation, std::tuple not supported)
    --'avr', -- Atmel/Microchip AVR (std::vector not supported)
    --'i686-w64-mingw32', -- Windows 32bit (eventfd not found)
    --'x86_64-w64-mingw32' -- Windows 64bit (eventfd not found)
}

for _, platform_name in ipairs(potential_platforms) do
    platform = make_platform(platform_name)
    try_build_package(usb_test, platform)
    try_build_package(dbus_test, platform)
end
