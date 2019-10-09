
#include <fibre/cpp_utils.hpp>
#include <fibre/print_utils.hpp>
#include <fibre/closure.hpp>
#include "test_utils.hpp"
#include <tuple>

void func0() {}
void func1(uint16_t, uint32_t) {}
int func2(std::tuple<>) { return 0; }
std::tuple<> func3(int) { return std::make_tuple(); }
std::tuple<int> func4(std::tuple<uint32_t, uint16_t>) { return std::make_tuple(1); }
std::tuple<uint16_t, uint32_t> func5(char, char) { return std::make_tuple(1, 2); }

std::tuple<uint16_t> identity_fn1(uint16_t a) { return std::make_tuple(a); }
std::tuple<uint16_t, uint32_t> identity_fn2(uint16_t a, uint32_t b) { return std::make_tuple(a, b); }
void add_one(int& a) { a++; }

static_assert(std::is_base_of<std::index_sequence<>, std::make_index_sequence<0>>::value, "index sequence broken");
static_assert(std::is_base_of<std::index_sequence<0>, std::make_index_sequence<1>>::value, "index sequence broken");
static_assert(std::is_base_of<std::index_sequence<0, 1>, std::make_index_sequence<2>>::value, "index sequence broken");
static_assert(std::is_base_of<std::index_sequence<0, 1, 2>, std::make_index_sequence<3>>::value, "index sequence broken");
static_assert(std::is_base_of<std::index_sequence<0, 1, 2, 3>, std::make_index_sequence<4>>::value, "index sequence broken");

static_assert(std::is_base_of<std::index_sequence<>, fibre::make_integer_sequence_from_to<size_t, 0, 0>>::value, "index range broken");
static_assert(std::is_base_of<std::index_sequence<>, fibre::make_integer_sequence_from_to<size_t, 5, 5>>::value, "index range broken");
static_assert(std::is_base_of<std::index_sequence<0, 1, 2, 3, 4>, fibre::make_integer_sequence_from_to<size_t, 0, 5>>::value, "index range broken");
static_assert(std::is_base_of<std::index_sequence<2, 3, 4>, fibre::make_integer_sequence_from_to<size_t, 2, 5>>::value, "index range broken");


static_assert(std::is_same<as_tuple_t<result_of_t<decltype(func1)>>, std::tuple<>>::value, "as_tuple_t or result_of_t broken");
static_assert(std::is_same<as_tuple_t<result_of_t<decltype(func2)>>, std::tuple<int>>::value, "as_tuple_t or result_of_t broken");
static_assert(std::is_same<as_tuple_t<result_of_t<decltype(func3)>>, std::tuple<>>::value, "as_tuple_t or result_of_t broken");
static_assert(std::is_same<as_tuple_t<result_of_t<decltype(func4)>>, std::tuple<int>>::value, "as_tuple_t or result_of_t broken");
static_assert(std::is_same<as_tuple_t<result_of_t<decltype(func5)>>, std::tuple<uint16_t, uint32_t>>::value, "as_tuple_t or result_of_t broken");

static_assert(std::is_same<args_of_t<decltype(func1)>, std::tuple<uint16_t, uint32_t>>::value, "args_of_t broken");
static_assert(std::is_same<args_of_t<decltype(func2)>, std::tuple<std::tuple<>>>::value, "args_of_t broken");
static_assert(std::is_same<args_of_t<decltype(func3)>, std::tuple<int>>::value, "args_of_t broken");
static_assert(std::is_same<args_of_t<decltype(func4)>, std::tuple<std::tuple<uint32_t, uint16_t>>>::value, "args_of_t broken");
static_assert(std::is_same<args_of_t<decltype(func5)>, std::tuple<char, char>>::value, "args_of_t broken");


static_assert(std::is_same<add_ptrs_to_tuple_t<std::tuple<>>, std::tuple<>>::value, "add_ptr_to_tuple_t broken");
static_assert(std::is_same<add_ptrs_to_tuple_t<std::tuple<int>>, std::tuple<int*>>::value, "add_ptr_to_tuple_t broken");
static_assert(std::is_same<add_ptrs_to_tuple_t<std::tuple<int*>>, std::tuple<int**>>::value, "add_ptr_to_tuple_t broken");
static_assert(std::is_same<add_ptrs_to_tuple_t<std::tuple<char, uint16_t****, uint32_t*>>, std::tuple<char*, uint16_t*****, uint32_t**>>::value, "add_ptr_to_tuple_t broken");


static_assert(!is_tuple<void>::value, "is_tuple broken");
static_assert(!is_tuple<int>::value, "is_tuple broken");
static_assert(is_tuple<std::tuple<>>::value, "is_tuple broken");
static_assert(is_tuple<std::tuple<int>>::value, "is_tuple broken");
static_assert(is_tuple<std::tuple<std::tuple<>>>::value, "is_tuple broken");

static_assert(std::is_same<tuple_take_t<0, std::tuple<>>, std::tuple<>>::value, "tuple_take_t broken");
static_assert(std::is_same<tuple_take_t<0, std::tuple<int>>, std::tuple<>>::value, "tuple_take_t broken");
static_assert(std::is_same<tuple_take_t<1, std::tuple<int>>, std::tuple<int>>::value, "tuple_take_t broken");
static_assert(std::is_same<tuple_take_t<1, std::tuple<uint8_t, uint16_t, uint32_t>>, std::tuple<uint8_t>>::value, "tuple_take_t broken");
static_assert(std::is_same<tuple_take_t<2, std::tuple<uint8_t, uint16_t, uint32_t>>, std::tuple<uint8_t, uint16_t>>::value, "tuple_take_t broken");

static_assert(std::is_same<tuple_skip_t<0, std::tuple<>>, std::tuple<>>::value, "tuple_skip_t broken");
static_assert(std::is_same<tuple_skip_t<0, std::tuple<int>>, std::tuple<int>>::value, "tuple_skip_t broken");
static_assert(std::is_same<tuple_skip_t<1, std::tuple<int>>, std::tuple<>>::value, "tuple_skip_t broken");
static_assert(std::is_same<tuple_skip_t<1, std::tuple<uint8_t, uint16_t, uint32_t>>, std::tuple<uint16_t, uint32_t>>::value, "tuple_skip_t broken");
static_assert(std::is_same<tuple_skip_t<2, std::tuple<uint8_t, uint16_t, uint32_t>>, std::tuple<uint32_t>>::value, "tuple_skip_t broken");

int main(int argc, const char** argv) {
    TestContext context;

    // TODO: add more print tests
    std::cout << "print test: " << std::tuple<uint16_t>(123) << std::endl;

    fibre::make_closure(func0)();

    TEST_EQUAL(fibre::make_closure(identity_fn1)(123), (std::make_tuple<uint16_t>(123)));
    TEST_EQUAL(fibre::make_closure(identity_fn1).bind(5234)(), (std::make_tuple<uint16_t>(5234)));
    TEST_EQUAL(fibre::make_closure(identity_fn2)(63, 21), (std::make_tuple<uint16_t, uint32_t>(63, 21)));
    TEST_EQUAL(fibre::make_closure(identity_fn2).bind(43)(-2), (std::make_tuple<uint16_t, uint32_t>(43, -2)));
    TEST_EQUAL(fibre::make_closure(identity_fn2).bind(-14890).bind(-9)(), (std::make_tuple<uint16_t, uint32_t>(-14890, -9)));

    int a = 5;
    fibre::make_closure(add_one).bind(a)();
    TEST_EQUAL(a, 6);

    return context.summarize();
}
