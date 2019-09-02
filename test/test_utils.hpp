#ifndef __FIBRE_TEST_UTILS_HPP
#define __FIBRE_TEST_UTILS_HPP

#include <stdlib.h>

// TODO: look into testing framework http://aceunit.sourceforge.net

template<typename TFunc, typename ... TCtx>
struct SafeContext {
    SafeContext(TFunc dtor, TCtx... ctx)
        : dtor_(dtor), ctx_(std::make_tuple(ctx...)) {}

    ~SafeContext() {
        std::apply(dtor_, ctx_);
    }
private:
    TFunc dtor_;
    std::tuple<TCtx...> ctx_;
};

template<typename TFunc, typename ... TCtx>
SafeContext<TFunc, TCtx...> on_leave_scope(TFunc dtor, TCtx... ctx) {
    return SafeContext<TFunc, TCtx...>(dtor, ctx...);
}


template<typename ... Ts>
bool test_assert(bool val, const char* file, size_t line, Ts... args) {
    if (!val) {
        fprintf(stderr, "error in %s:%zu: ", file, line);
        int dummy[sizeof...(Ts)] = { (std::cerr << args, 0)... };
        (void) dummy;
        fprintf(stderr, "\n");
        return false;
    }
    return true;
}

template<typename T>
bool test_zero(T val, const char* file, size_t line) {
    return test_assert(val == 0, __FILE__, __LINE__, "expected zero, got ", val);
}

template<typename T>
bool test_equal(T val1, T val2, const char* file, size_t line) {
    return test_assert(val1 == val2, __FILE__, __LINE__, "expected equal values, got ", val1, " and ", val2);
}

template<typename T>
bool test_not_equal(T val1, T val2, const char* file, size_t line) {
    return test_assert(val1 != val2, __FILE__, __LINE__, "expected unequal values, got ", val1, " and ", val2);
}

#define TEST_NOT_NULL(ptr)      do { if (!test_assert(ptr, __FILE__, __LINE__, "pointer is NULL")) return false; } while (0)
#define TEST_ZERO(val)          do { if (!test_zero(val, __FILE__, __LINE__)) return false; } while (0)
#define TEST_ASSERT(val, ...)   do { if (!test_assert(val, __FILE__, __LINE__, "assert failed")) return false; } while (0)
#define TEST_EQUAL(val1, val2)   do { if (!test_equal(val1, val2, __FILE__, __LINE__)) return false; } while (0)
#define TEST_NOT_EQUAL(val1, val2)   do { if (!test_not_equal(val1, val2, __FILE__, __LINE__)) return false; } while (0)



#endif // __FIBRE_TEST_UTILS_HPP