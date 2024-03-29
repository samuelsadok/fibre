
fibre_cpp_dir = '../cpp'
tup.include(fibre_cpp_dir..'/package.lua')

CXX='clang++'
LINKER='clang++'
CFLAGS={'-g', '-I.'}
LDFLAGS={}
object_files = {}


fibre_pkg = get_fibre_package({
    enable_server=true,
    enable_client=true,
    enable_event_loop=false,
    allow_heap=true,
    enable_libusb_backend=false,
    enable_tcp_client_backend=false,
    enable_tcp_server_backend=false,
})

CFLAGS += fibre_pkg.cflags
LDFLAGS += fibre_pkg.ldflags

for _, inc in pairs(fibre_pkg.include_dirs) do
    CFLAGS += '-I'..fibre_cpp_dir..'/'..inc
end

for _, src in pairs(fibre_pkg.code_files) do
    object_files += compile(fibre_cpp_dir..'/'..src)
end


autogen_pkg = fibre_autogen('../test/test-interface.yaml')

object_files += compile('sim_main.cpp')
object_files += compile('simulator.cpp')
object_files += compile('mock_can.cpp')
object_files += compile('../test/test_node.cpp', autogen_pkg.autogen_headers)

-- TODO: move up
for _, src in pairs(autogen_pkg.code_files) do
    object_files += compile(src, autogen_pkg.autogen_headers)
end


compile_outname='build/fibre_sim'

tup.frule{
    inputs=object_files,
    command='^c^ '..LINKER..' %f '..tostring(CFLAGS)..' '..tostring(LDFLAGS)..' -o %o',
    outputs={compile_outname}
}
