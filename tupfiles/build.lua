

function GCCToolchain(prefix, compiler_flags, linker_flags)

    -- add some default compiler flags
    compiler_flags += '-fstack-usage'

    gcc_generic_compiler = function(compiler, compiler_flags, src, flags, includes, outputs)
        -- add includes to CFLAGS
        for _,inc in pairs(includes) do
            flags += "-I"..inc
        end
        -- todo: vary build directory
        obj_file="build/"..src:gsub("/","_")..".o"
        su_file="build/"..src:gsub("/","_")..".su"
        tup.frule{
            inputs=src,
            command=compiler..' -c %f '..
                    tostring(compiler_flags)..' '.. -- CFLAGS for this compiler
                    tostring(flags).. -- CFLAGS for this translation unit
                    ' -o %o',
            outputs={obj_file,extra_outputs={su_file}}
        }
        outputs.object_files += obj_file
        outputs.su_files += su_file
    end
    return {
        compile_c = function(src, flags, includes, outputs) gcc_generic_compiler(prefix..'gcc', compiler_flags, src, flags, includes, outputs) end,
        compile_cpp = function(src, flags, includes, outputs) gcc_generic_compiler(prefix..'g++ -std=gnu++11', compiler_flags, src, flags, includes, outputs) end,
        link = function(objects, output_name)
            tup.frule{
                inputs=objects,
                command=prefix..'g++ %f '..
                        tostring(linker_flags)..
                        ' -o %o',
                outputs=output_name
            }
        end
    }
end

function LLVMToolchain(arch, compiler_flags, linker_flags)

    -- add some default compiler flags
    --compiler_flags += '-march='..arch
    compiler_flags += '-std=c++11'
    
    clang_generic_compiler = function(compiler, compiler_flags, src, flags, includes, outputs)
        -- add includes to CFLAGS
        for _,inc in pairs(includes) do
            flags += "-I"..inc
        end
        -- todo: vary build directory
        obj_file="build/"..src:gsub("/","_")..".o"
        tup.frule{
            inputs=src,
            command=compiler..' -c %f '..
                    tostring(compiler_flags)..' '.. -- CFLAGS for this compiler
                    tostring(flags).. -- CFLAGS for this translation unit
                    ' -o %o',
            outputs={obj_file}
        }
        outputs.object_files += obj_file
    end
    return {
        compile_c = function(src, flags, includes, outputs) clang_generic_compiler('clang', compiler_flags, src, flags, includes, outputs) end,
        compile_cpp = function(src, flags, includes, outputs) clang_generic_compiler('clang++', compiler_flags, src, flags, includes, outputs) end,
        link = function(objects, output_name)
            tup.frule{
                inputs=objects,
                command='clang++ %f '..
                        tostring(linker_flags)..
                        ' -o %o',
                outputs=output_name
            }
        end
    }
end

all_packages = {}


-- toolchains: Each element of this list is a collection of functions, such as compile_c, link, ...
--              You can create a new toolchain object for each platform you want to build for.
function build(args)
    if args.toolchain == nil then args.toolchain = {} end
    if args.sources == nil then args.sources = {} end
    if args.includes == nil then args.includes = {} end
    if args.packages == nil then args.packages = {} end
    if args.c_flags == nil then args.c_flags = {} end
    if args.cpp_flags == nil then args.cpp_flags = {} end
    if args.ld_flags == nil then args.ld_flags = {} end
    
    -- add includes of other packages
    for _,pkg_name in pairs(args.packages) do
        print('depend on package '..pkg_name)
        pkg = all_packages[pkg_name]
        if pkg == nil then
            error("unknown package "..pkg_name)
        end
        --tup.append_table(args.includes, pkg.includes)
        -- add path of each include
        for _,inc in pairs(pkg.includes or {}) do
            args.includes += tostring(inc)
        end
    end

    -- run everything once for every toolchain
    for _,toolchain in pairs(args.toolchains) do

        print(args.name)

        -- compile
        outputs = {}
        for _,src in pairs(args.sources) do
            print("compile "..src)
            if tup.ext(src) == 'c' then
                toolchain.compile_c(src, args.c_flags, args.includes, outputs)
            elseif tup.ext(src) == 'cpp' then
                toolchain.compile_cpp(src, args.cpp_flags, args.includes, outputs)
            else
                error('unrecognized file ending')
            end
        end

        -- link
        if outputs.object_files != nil then
            toolchain.link(outputs.object_files, args.name)
        end

        outputs.includes = {}
        for _,inc in pairs(args.includes) do
            table.insert(outputs.includes, tup.nodevariable(inc))
        end
        if args.name != nil then
            all_packages[args.name] = outputs
        end
    end

    for k,v in pairs(all_packages) do
        print('have package '..k)
    end
end

