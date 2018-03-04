
tup.include('tupfiles/build.lua')
tup.include('cpp/Tupfile.lua')

build{
    name='run_tests',
    --toolchains={GCCToolchain('', {'-Ofast', '-fvisibility=hidden', '-frename-registers', '-funroll-loops'}, {})},
    --toolchains={GCCToolchain('avr-', {'-Ofast', '-fvisibility=hidden', '-frename-registers', '-funroll-loops', '-I/home/samuel/stlport-avr/stlport'}, {})},
    toolchains={LLVMToolchain('x86_64', {'-O3', '-fno-sanitize=safe-stack', '-fno-stack-protector'}, {'-flto', '-Wl,-s'})},
    --toolchains={LLVMToolchain('avr', {'-O3', '-std=gnu++11', '--target=avr', '-fno-sanitize=safe-stack', '-fno-stack-protector', '-I/home/samuel/stlport-avr/stlport'}, {'-flto', '-Wl,-s'})},
    packages={'libfibre'},
    sources={'test/run_tests.cpp'}
}
