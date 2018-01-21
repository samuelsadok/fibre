
SOURCES = {'lightd.cpp','fibre/cpp/posix_udp.cpp','fibre/cpp/protocol.cpp'}
INCLUDE = {'fibre/cpp/include'}
LIBS = {'pthread'}
CFLAGS = {'-std=c++11'}
LDFLAGS = {}

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

-- Create compile rules
OBJECTS = {}
for _,src in pairs(SOURCES) do
    obj="build/"..src:gsub("/","_")..".o"
    tup.frule{
        inputs=src,
        command='arm-linux-gnueabihf-g++ -c %f '..
                tostring(CFLAGS)..
                ' -o %o',
        outputs=obj
    }
    OBJECTS += obj
end

tup.frule{
    inputs=OBJECTS,
    command='arm-linux-gnueabihf-g++ %f '..
            tostring(CFLAGS)..' '..
            tostring(LDFLAGS)..
            ' -o %o',
    outputs='build/lightd'
}

