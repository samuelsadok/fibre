
tup.include('../tupfiles/build.lua')

fibre_package = define_package{
    sources={
        'fibre.cpp',
        'input.cpp',
        'output.cpp',
        'remote_node.cpp',
        'posix_tcp.cpp'
    }, -- 'posix_udp.cpp'},
    libs={'pthread'},
    headers={'include'}
}
