
tup.include('../tupfiles/build.lua')

build{
    name='libfibre',
    toolchains={GCCToolchain('', {}, {})},
    --sources={'test_server.cpp'}
    includes={'include'}
}
