

-- Returns a table that contains the Fibre code files and the flags required to
-- compile and link those files.
--
-- args: A dictionary containing the fibre options. Possible keys are:
--       enable_server, enable_client, enable_event_loop, allow_heap, enable_libusb, max_log_verbosity
--       The meaning of these options is documented in README.md.
--       In addition:
--          pkgconfig: nil, a boolean or a string. If a string is provided, it
--                     names the "pkgconfig" binary that this function may
--                     invoke to find build dependencies. nil or a boolean of
--                     true is equal to providing "pkgconf". A boolean of false
--                     means the function may return an incomplete list of compile flags.
--
-- Returns: A dictionary with the following items:
--  code_files: A list of strings that name the C++ code files to be compiled.
--              The names are relative to package.lua.
--  include_dirs: A list of directories that must be added to the include path
--                when compiling the code files. The paths are relative to
--                package.lua.
--  cflags: A list of flags that should be passed to the compiler/linker when
--          compiling and linking the code files.
--  ldflags: A list of linker flags that should be passed to the linker when
--           linking the object files.
function get_fibre_package(args)
    pkg = {
        code_files = {
            --'libfibre.cpp',
            'fibre.cpp',
            --'legacy_protocol.cpp',
            'channel_discoverer.cpp',
        },
        include_dirs = {'include'},
        cflags = {},
        ldflags = {},
    }

    if args.pkgconf == true or args.pkgconf == nil then
        pkgconf_file = 'pkgconf'
    elseif args.pkgconf then
        pkgconf_file = args.pkgconf
    end

    function pkgconf(lib)
        if pkgconf_file then
            pkg.cflags += run_now(pkgconf_file..' '..lib..' --cflags')
            pkg.ldflags += run_now(pkgconf_file..' '..lib..' --libs')
        end
    end

    pkg.cflags += '-DFIBRE_ENABLE_SERVER='..(args.enable_server and '1' or '0')
    pkg.cflags += '-DFIBRE_ENABLE_CLIENT='..(args.enable_client and '1' or '0')
    pkg.cflags += '-DFIBRE_ENABLE_EVENT_LOOP='..(args.enable_event_loop and '1' or '0')
    pkg.cflags += '-DFIBRE_ALLOW_HEAP='..(args.allow_heap and '1' or '0')
    pkg.cflags += '-DFIBRE_MAX_LOG_VERBOSITY='..(args.max_log_verbosity or '5')
    pkg.cflags += '-DFIBRE_DEFAULT_LOG_VERBOSITY='..(args.default_log_verbosity or '2')
    pkg.cflags += '-DFIBRE_ENABLE_LIBUSB_BACKEND='..(args.enable_libusb_backend and '1' or '0')
    pkg.cflags += '-DFIBRE_ENABLE_TCP_SERVER_BACKEND='..(args.enable_tcp_server_backend and '1' or '0')
    pkg.cflags += '-DFIBRE_ENABLE_TCP_CLIENT_BACKEND='..(args.enable_tcp_client_backend and '1' or '0')

    if args.enable_libusb_backend then
        pkg.code_files += 'platform_support/libusb_transport.cpp'
        pkgconf("libusb-1.0")

        -- TODO: only add pthread on linux and windows
        pkg.ldflags += '-lpthread'
    end
    if args.max_log_verbosity == nil or (args.max_log_verbosity > 0) then
        pkg.code_files += 'logging.cpp'
    end
    if args.enable_client then
        pkg.code_files += 'legacy_object_client.cpp'
    end
    if args.enable_client or args.enable_server then
        pkg.code_files += 'legacy_protocol.cpp'
    end
    if args.enable_event_loop then
        pkg.code_files += 'platform_support/epoll_event_loop.cpp'
    end
    if args.enable_tcp_client_backend or args.enable_tcp_server_backend then
        -- TODO: chose between windows and posix backend
        pkg.code_files += 'platform_support/posix_tcp_backend.cpp'
        pkg.code_files += 'platform_support/posix_socket.cpp'
        pkg.ldflags += '-lanl'
    end

    return pkg
end

-- Runs the specified shell command immediately (not as part of the dependency
-- graph).
-- Returns the values (return_code, stdout) where stdout has the trailing new
-- line removed.
function run_now(command)
    local handle
    handle = io.popen(command)
    local output = handle:read("*a")
    local rc = {handle:close()}
    if not rc[1] then
        error("failed to invoke "..command)
    end
    return string.sub(output, 0, -2)
end
