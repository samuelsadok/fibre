--
-- This file provides a minimal build system for the Tup build processor.
-- 
-- The following definitions are used:
--
-- ## Platform ##
-- Represents a certain combination of operating system, compiler toolchain and
-- compiler settings (such as optimization flags, ...).
--
-- Helper functions exist to construct platform objects (make_platform).
--
-- A platform is represented as a table containing the following keys:
--  - c_compiler: A function which, given certain inputs, generates tup rules to
--                compile C source files to object files.
--  - cpp_compiler: Same as c_compiler, but for C++ source files
--  - asm_compiler: Same as c_compiler, but for ASM source files
--  - linker: A function which, given certain inputs, generates tup rules to
--            link multiple object files into a single binary.
-- Additional keys may be present.
-- If a platform doesn't support a certain operation, some or all of the above
-- functions may be nil.
-- 
-- ## Package ##
-- A package is represented a function "platform" => "package output".
-- Given a platform object, it shall generate the tup rules to generate the
-- corresponding package outputs. nil shall be returned if the package cannot be
-- generated. This may also be the case if no appropriate compiler for the
-- platform was found.
-- Several make_[...]_package are available to define certain types of packages.
--
-- ## Package Output ##
-- May contain the following keys:
--  - exported_obj_files: List of the paths of the object files that this package exports
--  - exported_lib_flags: List of the strings containing the "-l..." and "-L..." flags that this package exports
--  - exported_cpp_flags: List of CPPFLAGS (such as "-I..." flags) that this package exports
--  - exported_includes: List of include directory paths that this package exports
--

if not build_lua then
    build_lua = true

    all_packages = {}
    all_rules = {}
end


-- Lua helper functions --------------------------------------------------------

function table.contains(table, element)
    for _, value in pairs(table) do
        if value == element then
            return true
        end
    end
    return false
end

function shallow_copy(table)
    local new_table = {}
    for k, v in pairs(table) do
        new_table[k] = v
    end
    return new_table
end

-- Runs a shell command.
-- Returns a tuple {success, stdout-output-as-string}
function run_now(command)
    local handle
    handle = io.popen(command..' 2>/dev/null')
    local output = handle:read("*a")
    local rc = {handle:close()}
    return rc[1], output
end

function resolve_path(root, path)
    if type(path) == "table" then
        result = {}
        for _, v in pairs(path) do
            table.insert(result, resolve_path(root, v))
        end
        return result
    elseif type(path) == "string" then
        if path:find('^/') then
            return path
        else
            return root..'/'..path
        end
    else
        error("unknown type")
    end
end

--function file_exists(file)
--    local f = io.open(file, "r")
--    if f then f:close() end
--    return f ~= nil
--end


-- Package build helper functions ----------------------------------------------

-- Wrapper function for tup.frule(). Ensures that duplicate rules do not lead to
-- errors.
function add_rule(new_rule)
    key = tostring(new_rule.outputs).."  "..tostring((new_rule.outputs or {}).extra_outputs)
    existing_rule = all_rules[key]
    if existing_rule then
        if (existing_rule.command == new_rule.command) and (existing_rule.input == new_rule.input) and (tostring(existing_rule.inputs) == tostring(new_rule.inputs)) then
            -- an equal rule for this output already exists
        else
            error("a different rule for this output already exists")
        end
    else
        -- TODO: add one key per output file
        all_rules[key] = new_rule
        tup.frule(new_rule)
    end
end

-- Registers a package under a given name.
--
-- A package does not need to be registered but doing so allows other packages
-- to refer to it by name rather than as a Lua variable.
--
-- name: A string uniquely identifying the package.
-- pkg: A "package" function or "package output" table as defined in the
--      beginning of this file.
function register_package(name, pkg)
    if type(pkg) == "table" then
        local pkg_table = pkg
        pkg = function(platform)
            return pkg_table
        end
    end
    if type(pkg) == "function" then
        all_packages[name] = pkg
        print('registered package '..name)
    else
        error("invalid package type "..type(pkg))
    end
end

-- Generates the tup rules to build a package for a given plarform, if applicable.
--
-- pkg: The name the desired package (as given to register_package) or a
--      "package" function as defined in the beginning of this file or a
--      "package output" table as defined in the beginning of this file.
-- platform: The platform for which to build the package. Shall be a "platform"
--           table as defined in the beginning of this file.
--
-- Returns a "package output" table as defined in the beginning of this file or
-- nil if the package cannot be built.
function try_build_package(pkg, platform)
    if type(pkg) == "string" then
        pkg = all_packages[pkg]
    end
    if type(pkg) == "function" then
        pkg = pkg(platform)
    end
    if type(pkg) != "table" then
        --error("package output not a table, but "..type(pkg))
        return nil
    end
    return pkg
end

-- Generates the tup rules to build a set of packages for a given platform.
--
-- mandatory: A list of packages. If any of these packages cannot be built, the
--            script exits with an error message.
-- optional: A list of optional packages. If any of these packages cannot be
--           built, it's ignored.
-- platform: The platform for which to build the packages. Shall be a "platform"
--           table as defined in the beginning of this file.
-- Each package in "mandatory" and "optional" shall be given in a form accepted
-- by "try_build_package()".
--
-- Returns a table corresponding to the definition of "package output" given in
-- the beginning of this file. This output aggregates the output of all packages
-- that were processed successfully.
function build_packages(mandatory, optional, platform)
    local packages = {}
    tup.append_table(packages, mandatory)
    tup.append_table(packages, optional)

    local outputs = {}

    for _, dependency in pairs(packages) do
        local inner_outputs = try_build_package(dependency, platform)
        if inner_outputs ~= nil then
            for k, v in pairs(inner_outputs) do
                if outputs[k] == nil then
                    outputs[k] = {}
                end
                tup.append_table(outputs[k], v)
            end
        else
            is_mandatory = table.contains(mandatory, dependency)
            if is_mandatory then
                print("mandatory dependency "..dependency.." not satisfied")
                return nil -- fail this package if the dependency was mandatory
            else
                print("optional dependency "..dependency.." not satisfied")
            end
        end
    end

    return outputs
end


-- Platform helper functions ---------------------------------------------------

function make_gcc_compiler(command, builddir, default_flags, gen_su_file)
    default_flags += '-fstack-usage'

    if not run_now(command..' --version') then
        return nil -- compiler not found
    end

    return function(src, extra_inputs, flags, outputs)
        local obj_file = builddir.."/"..src:gsub("/","_"):gsub("%.","_")..".o"
        outputs.exported_obj_files += obj_file
        if gen_su_file then
            su_file = builddir.."/"..src:gsub("/","_"):gsub("%.","_")..".su"
            extra_outputs = { su_file }
            outputs.exported_su_files += su_file
        else
            extra_outputs = {}
        end

        add_rule{
            inputs= { src, extra_inputs = extra_inputs },
            command=command..' -c %f '..
                    tostring(default_flags)..' '.. -- CFLAGS for this compiler
                    tostring(flags).. -- CFLAGS for this translation unit
                    ' -o %o',
            outputs={obj_file,extra_outputs=extra_outputs}
        }
    end
end

function make_gcc_linker(ld_command, size_command, objcopy_command, builddir, main_extension)
    if not run_now(ld_command..' --version') then
        return nil -- compiler not found
    end
    
    return function(output_name, obj_files, linker_flags)
        local output_file = builddir..'/'..output_name

        tup.frule{
            inputs=obj_files,
            command=ld_command..' %f '..
                    tostring(linker_flags)..' '..
                    '-Wl,-Map=%O.map'..
                    ' -o %o',
            outputs={output_file..main_extension, extra_outputs={output_file..'.map'}}
        }
        -- display the size
        tup.frule{inputs={output_file..main_extension}, command=size_command..' %f'}
        -- create *.hex and *.bin output formats
        tup.frule{inputs={output_file..main_extension}, command=objcopy_command..' -O ihex %f %o', outputs={output_file..'.hex'}}
        tup.frule{inputs={output_file..main_extension}, command=objcopy_command..' -O binary -S %f %o', outputs={output_file..'.bin'}}
    end
end

-- Returns an object corresponding to the definition of "platform" given in the
-- beginning of this file.
function make_platform(input)
    local platform = {}
    if type(input) == "string" then
        platform.prefix = (input == "") and "" or (input.."-")
        builddir = (input == "") and "build" or ("build/"..input)
        name = input
    else
        error("invalid platform specifier")
    end

    if (platform.prefix == 'i686-w64-mingw32-' or platform.prefix == 'x86_64-w64-mingw32-') then
        platform_linker_flags = ' -static-libgcc -static-libstdc++ -static'
        platform.main_extension = '.exe'
    else
        platform_linker_flags = ''
        platform.main_extension = '.elf'
    end

    platform.c_compiler = make_gcc_compiler(platform.prefix..'gcc -std=c99 -g -O3', builddir, {}, true)
    platform.cpp_compiler = make_gcc_compiler(platform.prefix..'g++ -std=c++11 -g -O3', builddir, {}, true)
    platform.asm_compiler = make_gcc_compiler(platform.prefix..'gcc -x assembler-with-cpp', builddir, {}, false)
    platform.linker = make_gcc_linker(platform.prefix..'g++ -g -O3'..platform_linker_flags, platform.prefix..'size', platform.prefix..'objcopy', builddir, platform.main_extension)

    if run_now(platform.prefix..'pkg-config --version') then
        platform.pkg_config_path = platform.prefix..'pkg-config'
    else
        print("warning: platform "..name.." has no pkg-config")
    end

    return platform
end


-- Package factories -----------------------------------------------------------

-- An object package exports everything a linker needs to link an executable.
--
-- pkg: A table or a function(platform) returning a table containing the following keys:
--  - sources: A list of C, C++ or ASM files.
--  - include_dirs: A list of include directory paths needed to compile the
--                  source files. Include directories that come from another package should not be listed here.
--  - cpp_flags: A list of flags to pass to the C/C++/Asm preprocessor. "-D" and "-I" flags usually go here.
--  - c_flags: A list of flags to pass to the C compiler
--  - cxx_flags: A list of flags to pass to the C++ compiler
--  - asm_flags: A list of flags to pass to the ASM compiler
--  - depends: List of packages that this package depends on
--
-- All "..._flags" lists are additive: they add to the flags that are provided
-- by the platform configuration and dependencies.
--
-- Returns a function corresponding to the definition of "package" given in the
-- beginning of this file.
function make_obj_package(pkg)
    local called_from = tup.getcwd()
    return function(platform)
        if type(pkg) == "function" then
            pkg = pkg(platform)
        end
        if type(pkg) != "table" then
            error("unknown argument type "..type(pkg))
        end

        local root = pkg.root or called_from

        local sources = shallow_copy(pkg.sources or {})
        local extra_inputs = resolve_path(root, shallow_copy(pkg.extra_inputs or {}))
        local include_dirs = shallow_copy(pkg.include_dirs or {})
        local cpp_flags = shallow_copy(pkg.cpp_flags or {})
        local c_flags = shallow_copy(pkg.c_flags or {})
        local cxx_flags = shallow_copy(pkg.cxx_flags or {})
        local asm_flags = shallow_copy(pkg.asm_flags or {})

        print("have extra_inputs "..tostring(extra_inputs))

        local outputs = shallow_copy(pkg.outputs or {})
        outputs.exported_obj_files = outputs.exported_obj_files or {}
        outputs.exported_cpp_flags = outputs.exported_cpp_flags or {}
        outputs.exported_lib_flags = outputs.exported_lib_flags or {}
        outputs.exported_includes = resolve_path(root, outputs.exported_includes or {})
        outputs.exported_extra_inputs = resolve_path(root, outputs.exported_extra_inputs or {})

        dep_outputs = build_packages(pkg.depends or {}, pkg.optional_depends or {}, platform)
        tup.append_table(outputs.exported_obj_files, dep_outputs.exported_obj_files or {})
        tup.append_table(outputs.exported_lib_flags, dep_outputs.exported_lib_flags or {})
        tup.append_table(include_dirs, dep_outputs.exported_includes or {})
        tup.append_table(outputs.exported_includes, dep_outputs.exported_includes or {}) -- note: this is only necessary because header files of this package may be contaminated with including header files from dependencies
        tup.append_table(cpp_flags, dep_outputs.exported_cpp_flags or {})
        tup.append_table(outputs.exported_cpp_flags, dep_outputs.exported_cpp_flags or {}) -- note: this is only necessary because header files of this package may be contaminated with including header files from dependencies
        tup.append_table(extra_inputs, dep_outputs.exported_extra_inputs or {})
        tup.append_table(outputs.exported_extra_inputs, dep_outputs.exported_extra_inputs or {})

        -- convert include list to flags
        for _, inc in pairs(include_dirs) do
            cpp_flags += "-I"..resolve_path(root, inc)
        end

        tup.append_table(c_flags, cpp_flags)
        tup.append_table(cxx_flags, cpp_flags)
        tup.append_table(asm_flags, cpp_flags)

        for _, src in pairs(sources) do
            src = resolve_path(root, src)
            ext = tup.ext(src)
            if ext == 'c' then
                if not platform.c_compiler then return nil end
                platform.c_compiler(src, extra_inputs, c_flags, outputs)
            elseif ext == 'cpp' then
                if not platform.cpp_compiler then return nil end
                platform.cpp_compiler(src, extra_inputs, cxx_flags, outputs)
            elseif ext == 's' or tup.ext(src) == 'asm' then
                if not platform.asm_compiler then return nil end
                platform.asm_compiler(src, extra_inputs, asm_flags, outputs)
            else
                error('unrecognized file ending '..ext)
            end
        end

        return outputs
    end
end

-- An "exe" package outputs executable binaries, such as .elf, .exe or similar
-- files. It is equivalent to an "obj" package, with a final linking step added
-- in the end.
--
-- pkg: See "pkg" argument of "make_obj_package". One additional "name" key is
--      supported, defining the name of the output binary. If "name" is not
--      provided, the name is derived from the first source file name.
--
-- Returns a function corresponding to the definition of "package" given in the
-- beginning of this file.
function make_exe_package(pkg)
    local called_from = tup.getcwd()
    local name = pkg.name or tup.base(pkg.sources[0] or pkg.sources[1])
    local obj_pkg = make_obj_package(pkg)

    return function(platform)
        local outputs = {}

        obj_outputs = obj_pkg(platform)
        --dep_outputs = build_packages(pkg.depends or {}, pkg.optional_depends or {}, platform)

        platform.linker(name, obj_outputs.exported_obj_files or {}, obj_outputs.exported_lib_flags or {})

        return outputs
    end
end

-- An pkgconf package is based on invoking pkg-config. It typically does not
-- define any tup rules but exports a couple of compiler and linker flags.
--
-- name: The package name that should be passed to "pkg-config"
--
-- Returns a function corresponding to the definition of "package" given in the
-- beginning of this file.
function make_pkgconf_package(name)
    return function(platform)
        if not platform.pkg_config_path then
            return nil
        end

        local success, cpp_flags, libs
        success, cpp_flags = run_now(platform.pkg_config_path..' '..name..' --cflags')
        if not success then return nil end
        success, libs = run_now(platform.pkg_config_path..' '..name..' --libs')
        if not success then return nil end

        cpp_flags = cpp_flags:gsub("\n", "")
        libs = libs:gsub("\n", "")
        return {
            exported_cpp_flags = { cpp_flags },
            exported_lib_flags = { libs }
        }
    end
end


-- Definitions of well-known packages ------------------------------------------

register_package('libusb', make_pkgconf_package('libusb-1.0'))
register_package('libdbus', make_pkgconf_package('dbus-1'))
register_package('libudev', make_pkgconf_package('libudev'))
register_package('pthread', { exported_lib_flags = {'-lpthread'}})
