
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
TestContext test_pack_unpack_with_vals(Ts&&... vals) {
    TestContext context;

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

    return context;
}

TestContext test_pack_unpack() {
    TestContext context;

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
    
    TEST_ADD(test_pack_unpack_with_vals((std::string)"test string"));
    TEST_ADD(test_pack_unpack_with_vals((int16_t)0x9876));
    TEST_ADD(test_pack_unpack_with_vals((uint16_t)0x9876));
    TEST_ADD(test_pack_unpack_with_vals((int32_t)0x98765432));
    TEST_ADD(test_pack_unpack_with_vals((uint32_t)0x98765432));
    TEST_ADD(test_pack_unpack_with_vals((int64_t)0x9876543210FEDCBAULL)); // fails currently
    TEST_ADD(test_pack_unpack_with_vals((uint64_t)0x9876543210FEDCBAULL)); // fails currently

    TEST_ADD(test_pack_unpack_with_vals(std::vector<int>{1, 2, 3, 4, 5}));
    TEST_ADD(test_pack_unpack_with_vals(std::vector<std::string>{"test", "vector"}));

    TEST_ADD(test_pack_unpack_with_vals(std::variant<int, std::string>{5}));
    TEST_ADD(test_pack_unpack_with_vals(std::variant<int, std::string>{"asd"}));
    TEST_ADD(test_pack_unpack_with_vals(std::variant<std::string, int>{5}));
    TEST_ADD(test_pack_unpack_with_vals(std::variant<std::string, int>{"asd"}));

    TEST_ADD(test_pack_unpack_with_vals(std::unordered_map<std::string, int>{{"entry1", 1}, {"entry2", 2}}));
    TEST_ADD(test_pack_unpack_with_vals(std::unordered_map<std::string, int>{}));
    TEST_ADD(test_pack_unpack_with_vals(std::unordered_map<std::string, fibre::dbus_variant>{{"str_entry", "123"}, {"int_entry", 456}}));

    TEST_EQUAL(DBusRemoteObjectBase(nullptr, "my_service", "my_object"), DBusRemoteObjectBase(nullptr, "my_service", "my_object"));
    TEST_NOT_EQUAL(DBusRemoteObjectBase(nullptr, "my_service", "my_object"), DBusRemoteObjectBase(nullptr, "", "my_object"));
    TEST_ADD(test_pack_unpack_with_vals(DBusObjectPath("my_object")));

    // test print functions
    std::cout << "obj: " << DBusRemoteObjectBase(nullptr, "my_service", "my_object") << "\n";
    std::cout << "dict: " << std::unordered_map<DBusObjectPath, int>{{DBusObjectPath("my_object"), 1}} << "\n";
    TEST_EQUAL((std::unordered_map<DBusObjectPath, int>{{DBusObjectPath("my_object"), 1}}),
               (std::unordered_map<DBusObjectPath, int>{{DBusObjectPath("my_object"), 1}}));
    TEST_ADD(test_pack_unpack_with_vals(std::unordered_map<DBusObjectPath, int>{{DBusObjectPath("my_object"), 1}}));

    using fancy_type = std::unordered_map<DBusObjectPath, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>;
    fancy_type fancy_obj;
    TEST_ADD(test_pack_unpack_with_vals(fancy_obj));

    return context;
}

volatile uint32_t invoked_functions = 0;

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
        invoked_functions ^= 0x10;
        return {123, "ret val"};
    }
    std::tuple<std::string, uint32_t> Func6(int32_t in_arg1, std::string in_arg2) {
        std::cout << "Func6 called with " << in_arg1 << ", " << in_arg2 << "\n";
        if (in_arg1 == 4321 && in_arg2 == "blue")
            invoked_functions ^= 0x20;
        Signal1.trigger();
        Signal2.trigger(-5);
        Signal3.trigger(10, "apples");
        return {in_arg2 + "berry", in_arg1 + 5};
    }

    fibre::DBusSignal<> Signal1;
    fibre::DBusSignal<int32_t> Signal2;
    fibre::DBusSignal<int32_t, std::string> Signal3;
};


TestContext test_remote_object(DBusRemoteObject<io_fibre_TestInterface>& obj) {
    TestContext context;

    volatile uint32_t completed_functions = 0;

    static auto fn1_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*) {
            std::cout << "fn1 call complete\n";
            completed_functions ^= 0x1;
        }
    ).bind(completed_functions);
    static auto fn2_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*) {
            std::cout << "fn2 call complete\n";
            completed_functions ^= 0x2;
        }
    ).bind(completed_functions);
    static auto fn3_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*) {
            std::cout << "fn3 call complete\n";
            completed_functions ^= 0x4;
        }
    ).bind(completed_functions);
    static auto fn4_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*, int32_t ret_arg1) {
            std::cout << "fn4 call complete\n";
            if (ret_arg1 == 321)
                completed_functions ^= 0x8;
        }
    ).bind(completed_functions);
    static auto fn5_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*, int32_t ret_arg1, std::string ret_arg2) {
            std::cout << "fn5 call complete\n";
            if (ret_arg1 == 123 && ret_arg2 == "ret val")
                completed_functions ^= 0x10;
        }
    ).bind(completed_functions);
    static auto fn6_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*, std::string ret_arg1, uint32_t ret_arg2) {
            std::cout << "fn6 call complete\n";
            if (ret_arg1 == "blueberry" && ret_arg2 == 4326)
                completed_functions ^= 0x20;
        }
    ).bind(completed_functions);
    static auto sig1_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*) {
            std::cout << "sig1 triggered\n";
            completed_functions ^= 0x40;
        }
    ).bind(completed_functions);
    static auto sig2_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*, int32_t ret_arg1) {
            std::cout << "sig2 triggered\n";
            if (ret_arg1 == -5)
                completed_functions ^= 0x80;
        }
    ).bind(completed_functions);
    static auto sig3_callback = make_lambda_closure(
        [](volatile uint32_t& completed_functions, io_fibre_TestInterface*, int32_t ret_arg1, std::string ret_arg2) {
            std::cout << "sig3 triggered\n";
            if (ret_arg1 == 10 && ret_arg2 == "apples")
                completed_functions ^= 0x100;
        }
    ).bind(completed_functions);

    obj.Signal1 += &sig1_callback;
    obj.Signal2 += &sig2_callback;
    obj.Signal3 += &sig3_callback;

    // Send method calls over DBus
    TEST_ZERO(obj.Func1_async(&fn1_callback));
    TEST_ZERO(obj.Func2_async(1234, &fn2_callback));
    TEST_ZERO(obj.Func3_async(5678, "orange", &fn3_callback));
    TEST_ZERO(obj.Func4_async(&fn4_callback));
    TEST_ZERO(obj.Func5_async(&fn5_callback));
    TEST_ZERO(obj.Func6_async(4321, "blue", &fn6_callback));

    // Check if all 
    printf("waiting method calls to finish and signals to trigger...\n");
    usleep(1000000);
    TEST_EQUAL(completed_functions, (uint32_t)0x1ff);
    printf("done waiting\n");

    obj.Signal1 -= &sig1_callback;
    obj.Signal2 -= &sig2_callback;
    obj.Signal3 -= &sig3_callback;

    return context;
}

DBusRemoteObject<io_fibre_TestInterface>* discovered_remote_obj = nullptr;

static auto found_obj_callback = make_lambda_closure(
    [](DBusRemoteObject<io_fibre_TestInterface>* obj){
        // TODO: test if the "InterfacesAdded" signal carries an accurate snapshot of the properties
        discovered_remote_obj = obj;
    }
);
static auto lost_obj_callback = make_lambda_closure(
    [](DBusRemoteObject<io_fibre_TestInterface>* obj){
        discovered_remote_obj = nullptr;
    }
);

int main(int argc, const char** argv) {
    TestContext context;

    TEST_ADD(test_pack_unpack());

    Worker worker;
    if (TEST_ZERO(worker.init())) {

        DBusConnectionWrapper dbus_connection;
        if (TEST_ZERO(dbus_connection.init(&worker))) {
            std::string own_dbus_name = dbus_connection.get_name();

            // Expose an object with the TestInterface on DBus
            TestInterfaceImplementation local_test_object{};
            if (TEST_ZERO(dbus_connection.register_interfaces<io_fibre_TestInterface>(local_test_object, "/TestObject1"))) {

                // Optimistically instantiate a DBus proxy object for the object we just published
                DBusRemoteObject<io_fibre_TestInterface> remote_test_object({&dbus_connection, own_dbus_name, "/TestObject1"});
                TEST_ADD(test_remote_object(remote_test_object));
                TEST_EQUAL(invoked_functions, (uint32_t)0x3f);

                TEST_ZERO(dbus_connection.deregister_interfaces<io_fibre_TestInterface>("/TestObject1"));
            }

            DBusLocalObjectManager obj_mgr;
            if (TEST_ZERO(obj_mgr.init(&dbus_connection, "/obj_mgr"))) {

                TestInterfaceImplementation local_test_object{};
                if (TEST_ZERO(obj_mgr.add_interfaces<io_fibre_TestInterface>(local_test_object, "TestObject1"))) {

                    DBusDiscoverer<io_fibre_TestInterface> discoverer;
                    DBusRemoteObject<org_freedesktop_DBus_ObjectManager> remote_obj_mgr({&dbus_connection, own_dbus_name, "/obj_mgr"});
                    if (TEST_ZERO(discoverer.start(&remote_obj_mgr, &found_obj_callback, &lost_obj_callback))) {
                        usleep(1000000);
                        if (TEST_NOT_NULL(discovered_remote_obj)) {
                            TEST_EQUAL(discovered_remote_obj->base_.object_name_, std::string("/obj_mgr/TestObject1"));

                            // Instantiate a DBus proxy object for the object we just published
                            TEST_ADD(test_remote_object(*discovered_remote_obj));
                            TEST_EQUAL(invoked_functions, (uint32_t)0);

                            TEST_ZERO(obj_mgr.remove_interfaces<io_fibre_TestInterface>("TestObject1"));
                        }

                        // Try removing and adding back the local object and check if the discoverer follows along
                        usleep(1000000);
                        TEST_ZERO(discovered_remote_obj);
                        TEST_ZERO(obj_mgr.add_interfaces<io_fibre_TestInterface>(local_test_object, "TestObject1"));
                        usleep(1000000);
                        TEST_NOT_NULL(discovered_remote_obj);

                        TEST_ZERO(discoverer.stop());
                    }

                    TEST_ZERO(obj_mgr.remove_interfaces<io_fibre_TestInterface>("TestObject1"));
                }

                TEST_ZERO(obj_mgr.deinit());
            }

            TEST_ZERO(dbus_connection.deinit());
        }

        TEST_ZERO(worker.deinit());
    }

    return context.summarize();
}
