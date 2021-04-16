
function compile(src_file, extra_inputs)
    obj_file = 'build/'..tup.file(src_file)..'.o'
    tup.frule{
        inputs={src_file, extra_inputs=extra_inputs},
        command='^co^ '..CXX..' -c %f '..tostring(CFLAGS)..' -o %o',
        outputs={obj_file}
    }
    return obj_file
end
