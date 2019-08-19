

tup.include('../tupfiles/build.lua')
tup.include('../cpp/package.lua')

test_server = define_package{
    packages={fibre_package},
    sources={'test_server.cpp'}
}

test2 = define_package{
    packages={fibre_package},
    sources={'test2.cpp'}
}

dbus_test = define_package{
    packages={fibre_package},
    sources={'dbus_test.cpp'}
}

unit_tests = define_package{
    packages={fibre_package},
    sources={'run_tests.cpp'}
}


--toolchain=GCCToolchain('', 'build', {'-O3', '-fvisibility=hidden', '-frename-registers', '-funroll-loops'}, {})
toolchain=GCCToolchain('', 'build', {'-O3', '-g', '-Wall'}, {})
--toolchain=GCCToolchain('avr-', {'-Ofast', '-fvisibility=hidden', '-frename-registers', '-funroll-loops', '-I/home/samuel/stlport-avr/stlport'}, {})
--toolchain=LLVMToolchain('x86_64', {'-O3', '-fno-sanitize=safe-stack', '-fno-stack-protector'}, {'-flto', '-Wl,-s'})
--toolchain=LLVMToolchain('avr', {'-O3', '-std=gnu++11', '--target=avr', '-fno-sanitize=safe-stack', '-fno-stack-protector', '-I/home/samuel/stlport-avr/stlport'}, {'-flto', '-Wl,-s'})


--if tup.getconfig("BUILD_FIBRE_TESTS") == "true" then
--	build_executable('test_server', test_server, toolchain)
--	--build_executable('run_tests', unit_tests, toolchain)
--end

--build_executable('test2', test2, toolchain)
build_executable('dbus_test', dbus_test, toolchain)
