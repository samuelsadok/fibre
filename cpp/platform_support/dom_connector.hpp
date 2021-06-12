#ifndef __FIBRE_JS_INTEROP_HPP
#define __FIBRE_JS_INTEROP_HPP

#include <fibre/bufptr.hpp>
#include <fibre/callback.hpp>
#include <fibre/rich_status.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdint.h>
#include <memory>

struct JsStub;
class JsObjectTempRef;
using JsObjectRef = std::shared_ptr<JsObjectTempRef>;

extern "C" {
/**
 * @brief Increments the refcount of an opaque JavaScript object ID.
 */
extern void _js_ref(unsigned int obj);

/**
 * @brief Decrements the refcount of an opaque JavaScript object ID.
 * 
 * When the refcount reaches zero the object ID must not be used anymore.
 */
extern void _js_unref(unsigned int obj);

extern void _js_call_sync(unsigned int obj, const char* func, JsStub* args, size_t n_args);
extern void _js_call_async(unsigned int obj, const char* func, JsStub* args, size_t n_args, void(*callback)(void*, const JsStub&), void* ctx, unsigned int dict_depth);
extern void _js_get_property(unsigned int obj, const char* property, void(*callback)(void*, const JsStub&), void* ctx, unsigned int dict_depth);
extern void _js_set_property(unsigned int obj, const char* property, JsStub* arg);
}


enum class JsType {
    kUndefined = 0,
    kInt = 1,
    kString = 2,
    kList = 3,
    kDict = 4,
    kObject = 5,
    kFunc = 6,
    kArray = 7
};

struct JsStub {
    JsType type;
    uintptr_t val;
};

struct JsFuncStub {
    uintptr_t callback;
    uintptr_t ctx;
};

struct JsArrayStub {
    uintptr_t start;
    uintptr_t end;
};

struct JsUndefined {};
static const constexpr JsUndefined js_undefined{};

class JsTransferStorage {
public:
    JsStub* push(size_t n) {
        JsStub* ptr = new JsStub[n];
        stubs.push_back(ptr);
        return ptr;
    }
    ~JsTransferStorage() {
        for (auto ptr: stubs) {
            delete [] ptr;
        }
        for (auto ptr: funcs) {
            delete [] ptr;
        }
    }

    JsStub to_js(JsUndefined val) {
        return {JsType::kUndefined, 0};
    }

    JsStub to_js(int val) {
        return {JsType::kInt, (uintptr_t)val};
    }

    JsStub to_js(const std::string& val) {
        return {JsType::kString, (uintptr_t)val.data()};
    }

    template<typename T>
    JsStub to_js(const std::vector<T>& val) {
        JsStub* arr = push(val.size() + 1);
        JsStub* ptr = arr;
        *(ptr++) = to_js(val.size());
        for (auto& item : val) {
            *(ptr++) = to_js(item);
        }
        return {JsType::kList, (uintptr_t)arr};
    }

    template<typename TKey, typename TVal>
    JsStub to_js(const std::unordered_map<TKey, TVal>& val) {
        JsStub* arr = push(2 * val.size() + 1);
        JsStub* ptr = arr;
        *(ptr++) = to_js(val.size());
        for (auto& kv : val) {
            *(ptr++) = to_js(kv.first);
            *(ptr++) = to_js(kv.second);
        }
        return {JsType::kDict, (uintptr_t)arr};
    }

    JsStub to_js(fibre::Callback<void, const JsStub*, size_t> val) {
        JsFuncStub* func = new JsFuncStub{(uintptr_t)val.get_ptr(), (uintptr_t)val.get_ctx()};
        funcs.push_back(func);
        return {JsType::kFunc, (uintptr_t)func};
    }

    JsStub to_js(fibre::cbufptr_t buf) {
        JsArrayStub* arr = new JsArrayStub{(uintptr_t)buf.begin(), (uintptr_t)buf.end()};
        arrays.push_back(arr);
        return {JsType::kArray, (uintptr_t)arr};
    }
    
    std::vector<JsStub*> stubs;
    std::vector<JsFuncStub*> funcs;
    std::vector<JsArrayStub*> arrays;
};

class JsObjectTempRef {
public:
    JsObjectTempRef(unsigned int id) : id_(id) {}

    JsObjectRef ref() {
        JsObjectTempRef* ptr = new JsObjectTempRef(id_);
        _js_ref(ptr->id_);
        return std::shared_ptr<JsObjectTempRef>(ptr,
                                                [](JsObjectTempRef* ptr) {
                                                    _js_unref(ptr->id_);
                                                    delete ptr;
                                                });
    }

    template<typename TVal> fibre::RichStatus get_property(std::string property, TVal* result, unsigned int dict_depth = 0) {
        fibre::RichStatus status;
        fibre::Callback<void, const JsStub&> callback{
            [&](const JsStub& stub) {
                status = from_js(stub, result);
            }
        };
        _js_get_property(id_, property.data(), callback.get_ptr(),
                         callback.get_ctx(), dict_depth);
        return status;
    }

    template<typename TVal> void set_property(std::string property, TVal arg) {
        JsTransferStorage storage;
        JsStub stub = storage.to_js(arg);
        _js_set_property(id_, property.data(), &stub);
    }

    template<typename... TArgs>
    void call_sync(std::string func, TArgs... args) {
        JsTransferStorage storage;
        JsStub arg_arr[] = {to_js(storage, args)...};
        _js_call_sync(id_, func.data(), arg_arr, sizeof...(TArgs));
    }

    template<typename... TArgs>
    void call_async(std::string func,
                    fibre::Callback<void, const JsStub&> callback,
                    unsigned int dict_depth,
                    TArgs... args) {
        JsTransferStorage storage;
        JsStub stubs[] = {storage.to_js(args)...};
        _js_call_async(id_, func.data(), stubs, sizeof...(TArgs),
                       callback.get_ptr(), callback.get_ctx(), dict_depth);
    }

    unsigned int get_id() {
        return id_;
    }

private:
    JsObjectTempRef(const JsObjectTempRef&) = delete;

    unsigned int id_;
};

//static inline fibre::RichStatus from_js(const JsStub& stub, size_t* output) {
//    F_RET_IF(stub.type != JsType::kInt, "expected int type but got " << (int)stub.type);
//    *output = stub.val;
//    return fibre::RichStatus::success();
//}

//template<>
static inline fibre::RichStatus from_js(const JsStub& stub, JsStub* output) {
    *output = stub;
    return fibre::RichStatus::success();
}

template<typename T>
static inline typename std::enable_if<std::is_integral<T>::value, fibre::RichStatus>::type from_js(const JsStub& stub, T* output) {
    F_RET_IF(stub.type != JsType::kInt, "expected int type but got " << (int)stub.type);
    uint32_t val = stub.val;
    F_RET_IF(val > std::numeric_limits<T>::max(), val << " too large for T");
    *output = stub.val;
    return fibre::RichStatus::success();
}

static inline fibre::RichStatus from_js(const JsStub& stub, std::string* output) {
    F_RET_IF(stub.type != JsType::kString, "expected string type but got " << (int)stub.type);
    *output = (const char*)stub.val;
    return fibre::RichStatus::success();
}

template<typename T>
static inline fibre::RichStatus from_js(const JsStub& stub, std::vector<T>* output) {
    F_RET_IF(stub.type != JsType::kList, "expected list type but got " << (int)stub.type);
    JsStub* arr = (JsStub*)stub.val;
    size_t length;
    F_RET_IF_ERR(from_js(arr[0], &length), "in list length");
    output->resize(length);
    for (size_t i = 0; i < length; ++i) {
        F_RET_IF_ERR(from_js(arr[i + 1], &(*output)[i]), "in list element " << i);
    }
    return fibre::RichStatus::success();
}

template<typename TKey, typename TVal>
static inline fibre::RichStatus from_js(const JsStub& stub, std::unordered_map<TKey, TVal>* output) {
    F_RET_IF(stub.type != JsType::kDict, "expected dict type but got " << (int)stub.type);
    JsStub* arr = (JsStub*)stub.val;
    size_t length;
    F_RET_IF_ERR(from_js(arr[0], &length), "in dict length");
    for (size_t i = 0; i < length; ++i) {
        TKey key;
        TVal val;
        F_RET_IF_ERR(from_js(arr[2 * i + 1], &key), "in dict key " << i);
        F_RET_IF_ERR(from_js(arr[2 * i + 2], &val), "in dict val " << i);
        (*output)[key] = val;
    }
    return fibre::RichStatus::success();
}

static inline fibre::RichStatus from_js(const JsStub& stub, JsObjectTempRef* output) {
    F_RET_IF(stub.type != JsType::kObject, "expected object type but got " << (int)stub.type);
    *output = stub.val;
    return fibre::RichStatus::success();
}

static inline fibre::RichStatus from_js(const JsStub& stub, JsObjectRef* output) {
    JsObjectTempRef temp_ref(0);
    F_RET_IF_ERR(from_js(stub, &temp_ref), "failed to get object ref");
    *output = temp_ref.ref();
    return fibre::RichStatus::success();
}

static inline fibre::RichStatus from_js(const JsStub& stub, fibre::cbufptr_t* output) {
    // TODO: in some cases array transfer involves two copies: one on the JS side
    // from source to HEAP8 and one on the C++ side from heap to some ahead-of-
    // time allocated buffer. Maybe we can optimize this?
    F_RET_IF(stub.type != JsType::kArray, "expected array type but got " << (int)stub.type);
    JsArrayStub* array_stub = (JsArrayStub*)stub.val;
    *output = {(const unsigned char*)array_stub->start, (const unsigned char*)array_stub->end};
    return fibre::RichStatus::success();
}

#endif // __FIBRE_JS_INTEROP_HPP
