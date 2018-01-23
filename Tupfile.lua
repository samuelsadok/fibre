
CPP_SOURCES = {'lightd.cpp','fibre/cpp/posix_udp.cpp','fibre/cpp/protocol.cpp'}
INCLUDE = {'fibre/cpp/include'}
LIBS = {'pthread', 'rt'}
CFLAGS = {}
LDFLAGS = {}

TOOLCHAIN = 'arm-linux-gnueabihf-' -- cross-compile
--TOOLCHAIN = '' -- compile for the current architecture

-- C sources
C_SOURCES = {
    'rpi_ws281x/mailbox.c',
    'rpi_ws281x/ws2811.c',
    'rpi_ws281x/pwm.c',
    'rpi_ws281x/pcm.c',
    'rpi_ws281x/dma.c',
    'rpi_ws281x/rpihw.c'
}


-- to cross compile:
--CFLAGS += '-target armv6l-unknown-linux-gnueabihf --sysroot=/usr/arm-linux-gnueabihf'

-- Convert includes to CFLAGS
for _,inc in pairs(INCLUDE) do
    CFLAGS += "-I"..inc
end

-- Convert libs to LDFLAGS
for _,lib in pairs(LIBS) do
    LDFLAGS += "-l"..lib
end




function compile(compiler, flags, sources)
    objects = {}
    for _,src in pairs(sources) do
        obj="build/"..src:gsub("/","_")..".o"
        tup.frule{
            inputs=src,
            command=compiler..' -c %f '..
                    tostring(flags)..
                    ' -o %o',
            outputs=obj
        }
        objects += obj
    end
    return objects
end

c_obj = compile(TOOLCHAIN..'gcc', CFLAGS, C_SOURCES)
cpp_obj = compile(TOOLCHAIN..'g++ -std=c++11', CFLAGS, CPP_SOURCES)


objects = {}
for _,obj in pairs(c_obj) do objects += obj end
for _,obj in pairs(cpp_obj) do objects += obj end

tup.frule{
    inputs=objects,
    command='arm-linux-gnueabihf-g++ %f '..
            tostring(CFLAGS)..' '..
            tostring(LDFLAGS)..
            ' -o %o',
    outputs='build/lightd'
}
