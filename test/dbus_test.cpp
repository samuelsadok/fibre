
#include <fibre/dbus.hpp>
#include <dbus/dbus.h>
#include <sys/epoll.h>
#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <fibre/../../dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp>
#include <fibre/../../dbus_interfaces/io.fibre.TestInterface.hpp>
#include <fibre/print_utils.hpp>
#include <unistd.h>

#include "test_utils.hpp"

using namespace fibre;

template<typename ... Ts>
bool test_pack_unpack_with_vals(Ts&&... vals) {
    DBusMessage* msg = dbus_message_new_method_call(nullptr, nullptr, nullptr, nullptr);
    TEST_NOT_NULL(msg);
    auto ctx = on_leave_scope(dbus_message_unref, msg);
    
    DBusMessageIter iter_pack;
    dbus_message_iter_init_append(msg, &iter_pack);

    TEST_ZERO(pack_message(&iter_pack, vals...));

    // Copy to another place
    std::tuple<std::remove_reference_t<Ts>...> input_vals{vals...};
    
    DBusMessageIter iter_unpack;
    TEST_ASSERT(dbus_message_iter_init(msg, &iter_unpack));
    std::tuple<std::remove_reference_t<Ts>...> unpacked_vals;
    TEST_ZERO(unpack_message_to_tuple(&iter_unpack, unpacked_vals));

    //TEST_EQUAL(std::make_tuple(vals...), unpacked_vals);
    TEST_EQUAL(input_vals, unpacked_vals);

    return true;
}

bool test_pack_unpack() {
    printf("start test\n");

    std::variant<int, std::string> var = 5;
    std::cout << "variant is now " << var << std::endl;
    TEST_EQUAL(var.index(), (size_t)0);
    TEST_EQUAL(std::get<0>(var), 5);
    var = "abc2";
    std::cout << "variant is now " << var << std::endl;
    var = std::string("asd");
    std::cout << "variant is now " << var << std::endl;
    TEST_EQUAL(var.index(), (size_t)1);
    TEST_EQUAL(std::get<1>(var), (std::string)"asd");

    std::variant<int, short, std::string> new_variant;
    new_variant = 123;
    std::cout << "test1: variant is now " << new_variant << std::endl;
    new_variant = (short)345;
    std::cout << "test2: variant is now " << new_variant << std::endl;
    new_variant = "a string";
    std::cout << "test3: variant is now " << new_variant << std::endl;
    
    TEST_ASSERT(test_pack_unpack_with_vals((std::string)"test string"));
    TEST_ASSERT(test_pack_unpack_with_vals((int16_t)0x9876));
    TEST_ASSERT(test_pack_unpack_with_vals((uint16_t)0x9876));
    TEST_ASSERT(test_pack_unpack_with_vals((int32_t)0x98765432));
    TEST_ASSERT(test_pack_unpack_with_vals((uint32_t)0x98765432));
    TEST_ASSERT(test_pack_unpack_with_vals((int64_t)0x9876543210FEDCBAULL)); // fails currently
    TEST_ASSERT(test_pack_unpack_with_vals((uint64_t)0x9876543210FEDCBAULL)); // fails currently

    TEST_ASSERT(test_pack_unpack_with_vals(std::vector<int>{1, 2, 3, 4, 5}));
    TEST_ASSERT(test_pack_unpack_with_vals(std::vector<std::string>{"test", "vector"}));

    TEST_ASSERT(test_pack_unpack_with_vals(std::variant<int, std::string>{5}));
    TEST_ASSERT(test_pack_unpack_with_vals(std::variant<int, std::string>{"asd"}));
    TEST_ASSERT(test_pack_unpack_with_vals(std::variant<std::string, int>{5}));
    TEST_ASSERT(test_pack_unpack_with_vals(std::variant<std::string, int>{"asd"}));

    TEST_ASSERT(test_pack_unpack_with_vals(std::unordered_map<std::string, int>{{"entry1", 1}, {"entry2", 2}}));
    TEST_ASSERT(test_pack_unpack_with_vals(std::unordered_map<std::string, int>{}));
    TEST_ASSERT(test_pack_unpack_with_vals(std::unordered_map<std::string, fibre::dbus_variant>{{"str_entry", "123"}, {"int_entry", 456}}));

    TEST_EQUAL(DBusObject(nullptr, "my_service", "my_object"), DBusObject(nullptr, "my_service", "my_object"));
    TEST_NOT_EQUAL(DBusObject(nullptr, "my_service", "my_object"), DBusObject(nullptr, "", "my_object"));
    TEST_ASSERT(test_pack_unpack_with_vals(DBusObject(nullptr, "", "my_object")));

    std::cout << "obj: " << DBusObject(nullptr, "my_service", "my_object") << "\n";
    std::cout << "dict: " <<  std::unordered_map<DBusObject, int>{{DBusObject(nullptr, "my_service", "my_object"), 1}} << "\n";
    TEST_EQUAL((std::unordered_map<DBusObject, int>{{DBusObject(nullptr, "my_service", "my_object"), 1}}),
               (std::unordered_map<DBusObject, int>{{DBusObject(nullptr, "my_service", "my_object"), 1}}));
    TEST_ASSERT(test_pack_unpack_with_vals(std::unordered_map<DBusObject, int>{{DBusObject(nullptr, "", "my_object"), 1}}));

    using fancy_type = std::unordered_map<DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>;
    fancy_type fancy_obj;
    TEST_ASSERT(test_pack_unpack_with_vals(fancy_obj));

    printf("DBus tests succeeded!\n");
    return true;
}

volatile uint32_t invoked_functions = 0;
volatile uint32_t completed_functions = 0;


class TestInterfaceImplementation {
public:
    void Func1() {
        std::cout << "Func1 called\n";
        invoked_functions ^= 0x1;
    }
    void Func2(int32_t in_arg1) {
        std::cout << "Func2 called with " << in_arg1 << "\n";
        if (in_arg1 == 1234)
            invoked_functions ^= 0x2;
    }
    void Func3(int32_t in_arg1, std::string in_arg2) {
        std::cout << "Func3 called with " << in_arg1 << ", " << in_arg2 << "\n";
        if (in_arg1 == 5678 && in_arg2 == "orange")
            invoked_functions ^= 0x4;
    }
    int32_t Func4() {
        std::cout << "Func4 called";
        invoked_functions ^= 0x8;
        return 321;
    }
    std::tuple<int32_t, std::string> Func5() {
        std::cout << "Func5 called\n";
        invoked_functions |= 0x10;
        return {123, "ret val"};
    }
    std::tuple<std::string, uint32_t> Func6(int32_t in_arg1, std::string in_arg2) {
        std::cout << "Func6 called with " << in_arg1 << ", " << in_arg2 << "\n";
        if (in_arg1 == 4321 && in_arg2 == "blue")
            invoked_functions |= 0x20;
        return {in_arg2 + "berry", in_arg1 + 5};
    }
};


fibre::Callback<> fn1_callback = {
    [](void*) {
        std::cout << "fn1 call complete\n";
        completed_functions ^= 0x1;
    }, nullptr
};
fibre::Callback<> fn2_callback = {
    [](void*) {
        std::cout << "fn2 call complete\n";
        completed_functions ^= 0x2;
    }, nullptr
};
fibre::Callback<> fn3_callback = {
    [](void*) {
        std::cout << "fn3 call complete\n";
        completed_functions ^= 0x4;
    }, nullptr
};
fibre::Callback<int32_t> fn4_callback = {
    [](void*, int32_t ret_arg1) {
        std::cout << "fn4 call complete\n";
        if (ret_arg1 == 321)
            completed_functions ^= 0x8;
    }, nullptr
};
fibre::Callback<int32_t, std::string> fn5_callback = {
    [](void*, int32_t ret_arg1, std::string ret_arg2) {
        std::cout << "fn5 call complete\n";
        if (ret_arg1 == 123 && ret_arg2 == "ret val")
            completed_functions ^= 0x10;
    }, nullptr
};
fibre::Callback<std::string, uint32_t> fn6_callback = {
    [](void*, std::string ret_arg1, uint32_t ret_arg2) {
        std::cout << "fn6 call complete\n";
        if (ret_arg1 == "blueberry" && ret_arg2 == 4326)
            completed_functions ^= 0x20;
    }, nullptr
};


int main(int argc, const char** argv) {
    if (!test_pack_unpack()) {
        printf("test failed\n");
        return -1;
    }

    Worker worker;
    if (worker.init() != 0) {
        printf("worker init failed.\n");
        return -1;
    }

    DBusConnectionWrapper dbus_connection;
    if (dbus_connection.init(&worker) != 0) {
        printf("DBus init failed.\n");
        return -1;
    }

    // Construct and pubslish an object with the TestInterface
    TestInterfaceImplementation local_test_object{};
    dbus_connection.publish<io_fibre_TestInterface>("/io/fibre/TestObject1", local_test_object);

    // Instantiate a DBus proxy object for the object we just published
    const char* own_dbus_name = dbus_bus_get_unique_name(dbus_connection.get_libdbus_ptr());
    io_fibre_TestInterface remote_test_object(&dbus_connection, own_dbus_name, "/io/fibre/TestObject1");

    // Send method calls over DBus
    remote_test_object.Func1_async(&fn1_callback);
    remote_test_object.Func2_async(1234, &fn2_callback);
    remote_test_object.Func3_async(5678, "orange", &fn3_callback);
    remote_test_object.Func4_async(&fn4_callback);
    remote_test_object.Func5_async(&fn5_callback);
    remote_test_object.Func6_async(4321, "blue", &fn6_callback);

    printf("waiting for callbacks to arrive...\n");
    usleep(1000000);
    if (invoked_functions != 0x3f) {
        printf("not all functions were invoked\n");
        return -1;
    }
    if (completed_functions != 0x3f) {
        printf("not all functions returned\n");
        return -1;
    }
    printf("done...\n");

    // TODO unpublish object

    if (dbus_connection.deinit() != 0) {
        printf("Connection deinit failed.\n");
    }

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
    }

    printf("test passed!\n");
    return 0;
}
