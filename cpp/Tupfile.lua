-- Ruleset for the tup build system

CFLAGS = "-Wall -std=c++17 -Iinclude"
PREPEND_GCC_ERR_WITH_ABSOLUTE_PATH='   3>&1 1>&2 2>&3 | sed -e "s/^\\([^:]*:[0-9]*:[0-9]*:\\)/\\$(pwd | sed \'s/[&/\\]/\\\\&/g\')\\/\\1/" 3>&1 1>&2 2>&3'


-- compile!
function compile(source, output)
    tup.frule{
        inputs={source},
        command= 'g++ '..CFLAGS..' -c '..source..' -o '..output,
        outputs= {output}
    }
end

compile('__test_server.cpp', '__test_server.o')
compile('posix_tcp.cpp', 'posix_tcp.o')
compile('posix_udp.cpp', 'posix_udp.o')
compile('protocol.cpp', 'protocol.o')

-- link everything together
tup.frule{
    inputs = {'__test_server.o', 'posix_tcp.o', 'posix_udp.o', 'protocol.o'},
    command='g++ %f -pthread -o %o',
    outputs= {'__test'}
}