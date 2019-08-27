
#include <fibre/dbus.hpp>
#include <dbus/dbus.h>
#include <sys/epoll.h>
#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <fibre/../../dbus_interfaces/org.freedesktop.DBus.Introspectable.hpp>
#include <fibre/bluetooth_discoverer.hpp>
#include <fibre/print_utils.hpp>
#include <unistd.h>


using namespace fibre;


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

/*struct cmp {
    template<typename T>
    bool operator()(T a, T b) {
        return a == b;
    }

    bool operator()(const char* a, const char* b) {
        return strcmp(a, b) == 0;
    }

    template<typename T>
    bool operator()(std::vector<T> a, std::vector<T> b) {
        return std::equal(a.begin(), a.end(), b.begin(), [](T& a, T& b){ return cmp()(a, b); });
    }

    template<typename T, typename ... Ts>
    bool operator()(std::tuple<T, Ts...> a, std::tuple<T, Ts...> b) {
        return cmp()(std::get<0>(a), std::get<0>(b)) && cmp()(tuple_skip<1>(a), tuple_skip<1>(b));
    }

    template<typename ... Ts, size_t ... Is>
    bool cmp_variant(std::variant<Ts...> a, std::variant<Ts...> b, std::index_sequence<Is...>) {
        bool is_equal[sizeof...(Ts)] = { ((a.index() == Is) && (b.index() == Is) && cmp()(std::get<Is>(a), std::get<Is>(b)))... };
        for (size_t i = 0; i < sizeof...(Ts); ++i)
            if (is_equal[i])
                return true;
        return false;
    }

    template<typename ... Ts>
    bool operator()(std::variant<Ts...> a, std::variant<Ts...> b) {
        return cmp_variant(a, b, std::make_index_sequence<sizeof...(Ts)>());
    }
};*/

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

    /*printf("signature is %s\n", get_signature<int>());
    printf("signature is %s\n", get_signature<std::vector<int>>());
    printf("signature is %s\n", get_signature<std::variant<int>>());*/
    //printf("signature is %s\n");

    /*char aaa[] = "asd";
    if (cmp()("asd", aaa)) {
        printf("strings are equal\n");
    }*/

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


    /*printf("dispatch message...\n");
    org_freedesktop_DBus_Introspectable bluez_root_obj(&dbus_connection, "org.bluez", "/");
    //DBusObject bluez(&dbus_connection, "org.bluez", "/org/bluez");

    Callback<const char*> callback = {
        [](void*, const char* xml) { printf("XML: %s", xml); }, nullptr
    };
    bluez_root_obj.Introspect_async(&callback);
    bluez_root_obj.Introspect_async(&callback);
    bluez_root_obj.Introspect_async(&callback);*/

    BluetoothCentralSideDiscoverer bluetooth_discoverer;
    if (bluetooth_discoverer.init(&worker, &dbus_connection) != 0) {
        printf("Discoverer init failed\n");
        return -1;
    }

    void* ctx;
    if (bluetooth_discoverer.start_channel_discovery(nullptr, &ctx) != 0) {
        printf("Discoverer start failed\n");
        return -1;
    }

    printf("waiting for a bit...\n");
    //usleep(3000000);
    getchar(); // TODO: make stdin unbuffered
    printf("done...\n");




    if (bluetooth_discoverer.deinit() != 0) {
        printf("Discoverer deinit failed.\n");
    }

    if (dbus_connection.deinit() != 0) {
        printf("Connection deinit failed.\n");
    }

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
    }

    return 0;
}
