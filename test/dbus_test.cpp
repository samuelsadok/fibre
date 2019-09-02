
#include <fibre/dbus.hpp>
#include <dbus/dbus.h>
#include <sys/epoll.h>
#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <fibre/../../dbus_interfaces/org.freedesktop.DBus.ObjectManager.hpp>
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

volatile bool callback_did_complete = false;

using fancy_type = std::unordered_map<DBusObject, std::unordered_map<std::string, std::unordered_map<std::string, fibre::dbus_variant>>>;
fibre::Callback<fancy_type> callback = {
    [](void*, fancy_type objects) {
        printf("got %zu objects\n", objects.size());
        for (auto& it : objects) {
            std::cout << "key: " << it.first << ", value: " << it.second << std::endl;
        }
        callback_did_complete = true;
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
    
    org_freedesktop_DBus_ObjectManager root_obj(&dbus_connection, "org.bluez", "/");
    root_obj.GetManagedObjects_async(&callback);

    printf("For callback to arrive...\n");
    usleep(1000000);
    if (!callback_did_complete) {
        printf("no callback received\n");
        return -1;
    }
    printf("done...\n");


    if (dbus_connection.deinit() != 0) {
        printf("Connection deinit failed.\n");
    }

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
    }

    return 0;
}
