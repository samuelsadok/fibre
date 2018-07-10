
tup.include('../tupfiles/build.lua')

fibre_package = define_package{
    --sources={'protocol.cpp', 'posix_tcp.cpp', 'posix_udp.cpp'},
    sources={'protocol.cpp'},
    libs={'pthread'},
    headers={'include'}
}
