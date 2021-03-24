
fibre_cpp_dir = '../cpp'
tup.include(fibre_cpp_dir..'/package.lua')

python_command = 'python3 -B'

interface_generator = '../tools/interface_generator.py'
interface_yaml = 'test-interface.yaml'
root_interface = 'TestIntf1'

tup.frule{inputs={'../cpp/static_exports_template.j2'}, command=python_command..' '..interface_generator..' --definitions '..interface_yaml..' --template %f --output %o', outputs='autogen/static_exports.cpp'}
tup.frule{inputs={'../cpp/interfaces_template.j2'}, command=python_command..' '..interface_generator..' --definitions '..interface_yaml..' --template %f --output %o', outputs='autogen/interfaces.hpp'}
tup.frule{inputs={'../cpp/legacy_endpoints_template.j2'}, command=python_command..' '..interface_generator..' --definitions '..interface_yaml..' --template %f --output %o', outputs='autogen/endpoints.hpp'}


CXX='clang++'
LINKER='clang++'
CFLAGS={'-g'}
LDFLAGS={}

function compile(src_file)
    obj_file = 'build/'..tup.file(src_file)..'.o'
    tup.frule{
        inputs={src_file, extra_inputs={'autogen/interfaces.hpp', 'autogen/endpoints.hpp'}},
        command='^co^ '..CXX..' -c %f '..tostring(CFLAGS)..' -o %o',
        outputs={obj_file}
    }
    return obj_file
end

fibre_pkg = get_fibre_package({
    enable_server=true,
    enable_client=false,
    enable_event_loop=true,
    allow_heap=true,
    enable_libusb_backend=false,
    enable_tcp_client_backend=true,
    enable_tcp_server_backend=true,
})

CFLAGS += fibre_pkg.cflags
LDFLAGS += fibre_pkg.ldflags

for _, inc in pairs(fibre_pkg.include_dirs) do
    CFLAGS += '-I'..fibre_cpp_dir..'/'..inc
end

object_files = {}
object_files += compile('test_server.cpp')
object_files += compile('autogen/static_exports.cpp')
--object_files += compile('new_autogen/endpoints_new.cpp')

for _, src in pairs(fibre_pkg.code_files) do
    object_files += compile(fibre_cpp_dir..'/'..src, fibre_cpp_dir..'/'..src..'.o')
end

--compile('autogen/fibre_exports.cpp', 'build/autogen_fibre_exports.cpp.o')
--object_files += compile('autogen/fibre_exports.cpp')

compile_outname='build/test_server.elf'

tup.frule{
    inputs=object_files,
    command='^c^ '..LINKER..' %f '..tostring(CFLAGS)..' '..tostring(LDFLAGS)..' -o %o',
    outputs={compile_outname}
}
