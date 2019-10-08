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



struct TestContext {
    template<typename ... Ts>
    bool test_assert(bool val, const char* file, size_t line, Ts... args) {
        asserts++;
        if (!val) {
            // TODO: use logging stubs
            fprintf(stderr, "error in %s:%zu: ", file, line);
            int dummy[sizeof...(Ts)] = { (std::cerr << args, 0)... };
            (void) dummy;
            fprintf(stderr, "\n");
            fails++;
            return false;
        }
        return true;
    }

    template<typename T>
    bool test_zero(T val, const char* file, size_t line) {
        return test_assert(val == 0, file, line, "expected zero, got ", val);
    }

    template<typename T>
    bool test_equal(T val1, T val2, const char* file, size_t line) {
        return test_assert(val1 == val2, file, line, "expected equal values, got ", val1, " and ", val2);
    }

    template<typename T>
    bool test_not_equal(T val1, T val2, const char* file, size_t line) {
        return test_assert(val1 != val2, file, line, "expected unequal values, got ", val1, " and ", val2);
    }

    bool test_add(TestContext ctx, const char* file, size_t line) {
        asserts += ctx.asserts;
        fails += ctx.fails;
        if (ctx.fails) {
            fprintf(stderr, "%d errors above were in %s:%zu\n", ctx.fails, file, line);
        }
        return ctx.fails == 0;
    }

    int summarize() {
        if (fails) {
            fprintf(stderr, "%d out of %d asserts failed!\n", fails, asserts);
            return -1;
        } else {
            fprintf(stderr, "All tests passed (%d asserts)!\n", asserts);
            return 0;
        }
    }

    size_t asserts = 0;
    size_t fails = 0;
};

#define TEST_ADD(subctx)            context.test_add(subctx, __FILE__, __LINE__)
#define TEST_NOT_NULL(ptr)          context.test_assert(ptr, __FILE__, __LINE__, "pointer is NULL")
#define TEST_ZERO(val)              context.test_zero(val, __FILE__, __LINE__)
#define TEST_ASSERT(val, ...)       context.test_assert(val, __FILE__, __LINE__, "assert failed")
#define TEST_EQUAL(val1, val2)      context.test_equal(val1, val2, __FILE__, __LINE__)
#define TEST_NOT_EQUAL(val1, val2)  context.test_not_equal(val1, val2, __FILE__, __LINE__)




#endif // __FIBRE_TEST_UTILS_HPP