
tup.include('tupfiles/build.lua')
tup.include('cpp/Tupfile.lua')

test_server = define_package{
    packages={fibre_package},
    sources={'test/test_server.cpp'}
}


toolchain=GCCToolchain('', 'build', {'-Ofast', '-fvisibility=hidden', '-frename-registers', '-funroll-loops'}, {})
--toolchain=GCCToolchain('avr-', {'-Ofast', '-fvisibility=hidden', '-frename-registers', '-funroll-loops', '-I/home/samuel/stlport-avr/stlport'}, {})
--toolchain=LLVMToolchain('x86_64', {'-O3', '-fno-sanitize=safe-stack', '-fno-stack-protector'}, {'-flto', '-Wl,-s'})
--toolchain=LLVMToolchain('avr', {'-O3', '-std=gnu++11', '--target=avr', '-fno-sanitize=safe-stack', '-fno-stack-protector', '-I/home/samuel/stlport-avr/stlport'}, {'-flto', '-Wl,-s'})

build_executable('test_server', test_server, toolchain)
