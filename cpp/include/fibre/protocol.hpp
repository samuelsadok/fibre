/*
see protocol.md for the protocol specification
*/

#ifndef __PROTOCOL_HPP
#define __PROTOCOL_HPP

#ifndef __FIBRE_HPP
#error "This file should not be included directly. Include fibre.hpp instead."
#endif

// TODO: resolve assert
//#define assert(expr)

#include <functional>
#include <limits>
#include <vector>
#include <optional>
#include <algorithm>
//#include <stdint.h>
#include <string.h>
#include <unordered_map>
#include "stream.hpp"
#include "uuid.hpp"
#include "crc.hpp"
#include "cpp_utils.hpp"


// Default CRC-8 Polynomial: x^8 + x^5 + x^4 + x^2 + x + 1
// Can protect a 4 byte payload against toggling of up to 5 bits
//  source: https://users.ece.cmu.edu/~koopman/crc/index.html
constexpr uint8_t CANONICAL_CRC8_POLYNOMIAL = 0x37;
constexpr uint8_t CANONICAL_CRC8_INIT = 0x42;

constexpr size_t CRC8_BLOCKSIZE = 4;

// Default CRC-16 Polynomial: 0x9eb2 x^16 + x^13 + x^12 + x^11 + x^10 + x^8 + x^6 + x^5 + x^2 + 1
// Can protect a 135 byte payload against toggling of up to 5 bits
//  source: https://users.ece.cmu.edu/~koopman/crc/index.html
// Also known as CRC-16-DNP
constexpr uint16_t CANONICAL_CRC16_POLYNOMIAL = 0x3d65;
constexpr uint16_t CANONICAL_CRC16_INIT = 0x1337;

constexpr uint8_t CANONICAL_PREFIX = 0xAA;





/* move to fibre_config.h ******************************/

typedef size_t endpoint_id_t;

struct ReceiverState {
    endpoint_id_t endpoint_id;
    size_t length;
    uint16_t seqno_thread;
    uint16_t seqno;
    bool expect_ack;
    bool expect_response;
    bool enforce_ordering;
};

/*******************************************************/



#include <unistd.h>

constexpr uint16_t PROTOCOL_VERSION = 1;

// Maximum time we allocate for processing and responding to a request
constexpr uint32_t PROTOCOL_SERVER_TIMEOUT_MS = 10;


namespace fibre {

/**
 * @brief Implements a StreamSink that calculates the CRC16 checksum
 * on the data that is sent to it.
 * This stream never closes.
 */
class CRC16Calculator : public StreamSink {
public:
    CRC16Calculator(uint16_t crc16_init) : crc16_(crc16_init) {}

    status_t process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) {
        crc16_ = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc16_, buffer, length);
        if (processed_bytes)
            *processed_bytes += length;
        return OK;
    }

    size_t get_min_non_blocking_bytes() const final { return SIZE_MAX; }

    uint16_t get_crc16() { return crc16_; }
private:
    uint16_t crc16_;
};

}


// @brief Endpoint request handler
//
// When passed a valid endpoint context, implementing functions shall handle an
// endpoint read/write request by reading the provided input data and filling in
// output data. The exact semantics of this function depends on the corresponding
// endpoint's specification.
//
// @param input: pointer to the input data
// @param input_length: number of available input bytes
// @param output: The stream where to write the output to. Can be null.
//                The handler shall abort as soon as the stream returns
//                a non-zero error code on write.
typedef std::function<void(void* ctx, const uint8_t* input, size_t input_length, fibre::StreamSink* output)> EndpointHandler;


template<typename T>
void default_readwrite_endpoint_handler(const T* value, const uint8_t* input, size_t input_length, fibre::StreamSink* output) {
    // If the old value was requested, call the corresponding little endian serialization function
    if (output) {
        // TODO: make buffer size dependent on the type
        uint8_t buffer[sizeof(T)];
        size_t cnt = write_le<T>(*value, buffer);
        // TODO: force non-blocking
        output->process_bytes(buffer, cnt, nullptr);
    }
}

template<typename T>
void default_readwrite_endpoint_handler(T* value, const uint8_t* input, size_t input_length, fibre::StreamSink* output) {
    // Read the endpoint value into output
    default_readwrite_endpoint_handler<T>(const_cast<const T*>(value), input, input_length, output);
    
    // If a new value was passed, call the corresponding little endian deserialization function
    uint8_t buffer[sizeof(T)] = { 0 }; // TODO: make buffer size dependent on the type
    if (input_length >= sizeof(buffer))
        read_le<T>(value, input);
}



template<typename T>
static inline const char* get_default_json_modifier();

template<>
inline constexpr const char* get_default_json_modifier<const float>() {
    return "\"type\":\"float\",\"access\":\"r\"";
}
template<>
inline constexpr const char* get_default_json_modifier<float>() {
    return "\"type\":\"float\",\"access\":\"rw\"";
}
template<>
inline constexpr const char* get_default_json_modifier<const uint64_t>() {
    return "\"type\":\"uint64\",\"access\":\"r\"";
}
template<>
inline constexpr const char* get_default_json_modifier<uint64_t>() {
    return "\"type\":\"uint64\",\"access\":\"rw\"";
}
template<>
inline constexpr const char* get_default_json_modifier<const int32_t>() {
    return "\"type\":\"int32\",\"access\":\"r\"";
}
template<>
inline constexpr const char* get_default_json_modifier<int32_t>() {
    return "\"type\":\"int32\",\"access\":\"rw\"";
}
template<>
inline constexpr const char* get_default_json_modifier<const uint32_t>() {
    return "\"type\":\"uint32\",\"access\":\"r\"";
}
template<>
inline constexpr const char* get_default_json_modifier<uint32_t>() {
    return "\"type\":\"uint32\",\"access\":\"rw\"";
}
template<>
inline constexpr const char* get_default_json_modifier<const uint16_t>() {
    return "\"type\":\"uint16\",\"access\":\"r\"";
}
template<>
inline constexpr const char* get_default_json_modifier<uint16_t>() {
    return "\"type\":\"uint16\",\"access\":\"rw\"";
}
template<>
inline constexpr const char* get_default_json_modifier<const uint8_t>() {
    return "\"type\":\"uint8\",\"access\":\"r\"";
}
template<>
inline constexpr const char* get_default_json_modifier<uint8_t>() {
    return "\"type\":\"uint8\",\"access\":\"rw\"";
}
template<>
inline constexpr const char* get_default_json_modifier<const bool>() {
    return "\"type\":\"bool\",\"access\":\"r\"";
}
template<>
inline constexpr const char* get_default_json_modifier<bool>() {
    return "\"type\":\"bool\",\"access\":\"rw\"";
}

class Endpoint {
public:
    //const char* const name_;
    virtual void handle(const uint8_t* input, size_t input_length, fibre::StreamSink* output) = 0;
    virtual bool get_string(char * output, size_t length) { return false; };
    virtual bool set_string(char * buffer, size_t length) { return false; }
};

static inline int write_string(const char* str, fibre::StreamSink* output) {
    return output->process_bytes(reinterpret_cast<const uint8_t*>(str), strlen(str), nullptr);
}


///* @brief Handles the communication protocol on one channel.
//*
//* When instantiated with a list of endpoints and an output packet sink,
//* objects of this class will handle packets passed into process_packet,
//* pass the relevant data to the corresponding endpoints and dispatch response
//* packets on the output.
//*/
//class BidirectionalPacketBasedChannel : public PacketSink {
//public:
//    BidirectionalPacketBasedChannel(PacketSink& output) :
//        output_(output)
//    { }
//
//    //size_t get_mtu() {
//    //    return SIZE_MAX;
//    //}
//    int process_packet(const uint8_t* buffer, size_t length);
//private:
//    PacketSink& output_;
//    uint8_t tx_buf_[TX_BUF_SIZE];
//};


/* ToString / FromString functions -------------------------------------------*/
/*
* These functions are currently not used by Fibre and only here to
* support the ODrive ASCII protocol.
* TODO: find a general way for client code to augment endpoints with custom
* functions
*/

template<typename T>
struct format_traits_t;

template<> struct format_traits_t<float> { using type = void;
    static constexpr const char * fmt = "%f";
    static constexpr const char * fmtp = "%f";
};
template<> struct format_traits_t<int32_t> { using type = void;
    static constexpr const char * fmt = "%ld";
    static constexpr const char * fmtp = "%ld";
};
template<> struct format_traits_t<uint32_t> { using type = void;
    static constexpr const char * fmt = "%lu";
    static constexpr const char * fmtp = "%lu";
};
template<> struct format_traits_t<int16_t> { using type = void;
    static constexpr const char * fmt = "%hd";
    static constexpr const char * fmtp = "%hd";
};
template<> struct format_traits_t<uint16_t> { using type = void;
    static constexpr const char * fmt = "%hu";
    static constexpr const char * fmtp = "%hu";
};
template<> struct format_traits_t<int8_t> { using type = void;
    static constexpr const char * fmt = "%hhd";
    static constexpr const char * fmtp = "%d";
};
template<> struct format_traits_t<uint8_t> { using type = void;
    static constexpr const char * fmt = "%hhu";
    static constexpr const char * fmtp = "%u";
};

template<typename T, typename = typename format_traits_t<T>::type>
static bool to_string(const T& value, char * buffer, size_t length, int) {
    snprintf(buffer, length, format_traits_t<T>::fmtp, value);
    return true;
}
template<typename T>
//__attribute__((__unused__))
static bool to_string(const bool& value, char * buffer, size_t length, int) {
    buffer[0] = value ? '1' : '0';
    buffer[1] = 0;
    return true;
}
template<typename T>
static bool to_string(const T& value, char * buffer, size_t length, ...) {
    return false;
}

template<typename T, typename = typename format_traits_t<T>::type>
static bool from_string(const char * buffer, size_t length, T* property, int) {
    return sscanf(buffer, format_traits_t<T>::fmt, property) == 1;
}
//__attribute__((__unused__))
template<typename T>
static bool from_string(const char * buffer, size_t length, bool* property, int) {
    int val;
    if (sscanf(buffer, "%d", &val) != 1)
        return false;
    *property = val;
    return true;
}
template<typename T>
static bool from_string(const char * buffer, size_t length, T* property, ...) {
    return false;
}


/* Object tree ---------------------------------------------------------------*/

template<typename ... TMembers>
struct MemberList;

template<>
struct MemberList<> {
public:
    static constexpr size_t endpoint_count = 0;
    static constexpr bool is_empty = true;
    void write_json(size_t id, fibre::StreamSink* output) {
        // no action
    }
    void register_endpoints(Endpoint** list, size_t id, size_t length) {
        // no action
    }
    Endpoint* get_by_name(const char * name, size_t length) {
        return nullptr;
    }
    std::tuple<> get_names_as_tuple() const { return std::tuple<>(); }
};

template<typename TMember, typename ... TMembers>
struct MemberList<TMember, TMembers...> {
public:
    static constexpr size_t endpoint_count = TMember::endpoint_count + MemberList<TMembers...>::endpoint_count;
    static constexpr bool is_empty = false;

    MemberList(TMember&& this_member, TMembers&&... subsequent_members) :
        this_member_(std::forward<TMember>(this_member)),
        subsequent_members_(std::forward<TMembers>(subsequent_members)...) {}

    MemberList(TMember&& this_member, MemberList<TMembers...>&& subsequent_members) :
        this_member_(std::forward<TMember>(this_member)),
        subsequent_members_(std::forward<MemberList<TMembers...>>(subsequent_members)) {}

    // @brief Move constructor
/*    MemberList(MemberList&& other) :
        this_member_(std::move(other.this_member_)),
        subsequent_members_(std::move(other.subsequent_members_)) {}*/

    void write_json(size_t id, fibre::StreamSink* output) /*final*/ {
        this_member_.write_json(id, output);
        if (!MemberList<TMembers...>::is_empty)
            write_string(",", output);
        subsequent_members_.write_json(id + TMember::endpoint_count, output);
    }

    Endpoint* get_by_name(const char * name, size_t length) {
        Endpoint* result = this_member_.get_by_name(name, length);
        if (result) return result;
        else return subsequent_members_.get_by_name(name, length);
    }

    void register_endpoints(Endpoint** list, size_t id, size_t length) /*final*/ {
        this_member_.register_endpoints(list, id, length);
        subsequent_members_.register_endpoints(list, id + TMember::endpoint_count, length);
    }

    TMember this_member_;
    MemberList<TMembers...> subsequent_members_;
};

template<typename ... TMembers>
MemberList<TMembers...> make_fibre_member_list(TMembers&&... member_list) {
    return MemberList<TMembers...>(std::forward<TMembers>(member_list)...);
}

template<typename ... TMembers>
class FibreObject {
public:
    FibreObject(const char * name, TMembers&&... member_list) :
        name_(name),
        member_list_(std::forward<TMembers>(member_list)...) {}

    static constexpr size_t endpoint_count = MemberList<TMembers...>::endpoint_count;

    void write_json(size_t id, fibre::StreamSink* output) {
        write_string("{\"name\":\"", output);
        write_string(name_, output);
        write_string("\",\"type\":\"object\",\"members\":[", output);
        member_list_.write_json(id, output),
        write_string("]}", output);
    }

    Endpoint* get_by_name(const char * name, size_t length) {
        size_t segment_length = strlen(name);
        if (!strncmp(name, name_, length))
            return member_list_.get_by_name(name + segment_length + 1, length - segment_length - 1);
        else
            return nullptr;
    }

    void register_endpoints(Endpoint** list, size_t id, size_t length) {
        member_list_.register_endpoints(list, id, length);
    }
    
    const char * name_;
    MemberList<TMembers...> member_list_;
};

template<typename ... TMembers>
FibreObject<TMembers...> make_fibre_object(const char * name, TMembers&&... member_list) {
    return FibreObject<TMembers...>(name, std::forward<TMembers>(member_list)...);
}

template<typename TProperty>
class FibreProperty : public Endpoint {
public:
    static constexpr const char * json_modifier = get_default_json_modifier<TProperty>();
    static constexpr size_t endpoint_count = 1;

    FibreProperty(const char * name, TProperty* property)
        : name_(name), property_(property)
    {}

/*  TODO: find out why the move constructor is not used when it could be
    FibreProperty(const FibreProperty&) = delete;
    // @brief Move constructor
    FibreProperty(FibreProperty&& other) :
        Endpoint(std::move(other)),
        name_(std::move(other.name_)),
        property_(other.property_)
    {}
    constexpr FibreProperty& operator=(const FibreProperty& other) = delete;
    constexpr FibreProperty& operator=(const FibreProperty& other) {
        //Endpoint(std::move(other)),
        //name_(std::move(other.name_)),
        //property_(other.property_)
        name_ = other.name_;
        property_ = other.property_;
        return *this;
    }
    FibreProperty& operator=(FibreProperty&& other)
        : name_(other.name_), property_(other.property_)
    {}
    FibreProperty& operator=(const FibreProperty& other)
        : name_(other.name_), property_(other.property_)
    {}*/

    void write_json(size_t id, fibre::StreamSink* output) {
        // write name
        write_string("{\"name\":\"", output);
        LOG_FIBRE("json: this at %x, name at %x is s\r\n", (uintptr_t)this, (uintptr_t)name_);
        //LOG_FIBRE("json\r\n");
        write_string(name_, output);

        // write endpoint ID
        write_string("\",\"id\":", output);
        char id_buf[10];
        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)id); // TODO: get rid of printf
        write_string(id_buf, output);

        // write additional JSON data
        if (json_modifier && json_modifier[0]) {
            write_string(",", output);
            write_string(json_modifier, output);
        }

        write_string("}", output);
    }

    // special-purpose function - to be moved
    Endpoint* get_by_name(const char * name, size_t length) {
        if (!strncmp(name, name_, length))
            return this;
        else
            return nullptr;
    }

    // special-purpose function - to be moved
    bool get_string(char * buffer, size_t length) final {
        return to_string(*property_, buffer, length, 0);
    }

    // special-purpose function - to be moved
    bool set_string(char * buffer, size_t length) final {
        return from_string(buffer, length, property_, 0);
    }

    void register_endpoints(Endpoint** list, size_t id, size_t length) {
        if (id < length)
            list[id] = this;
    }
    void handle(const uint8_t* input, size_t input_length, fibre::StreamSink* output) final {
        default_readwrite_endpoint_handler(property_, input, input_length, output);
    }
    /*void handle(const uint8_t* input, size_t input_length, fibre::StreamSink* output) {
        handle(input, input_length, output);
    }*/

    const char * name_;
    TProperty* property_;
};

// Non-const non-enum types
template<typename TProperty, ENABLE_IF(!std::is_enum<TProperty>::value)>
FibreProperty<TProperty> make_fibre_property(const char * name, TProperty* property) {
    return FibreProperty<TProperty>(name, property);
};

// Const non-enum types
template<typename TProperty, ENABLE_IF(!std::is_enum<TProperty>::value)>
FibreProperty<const TProperty> make_fibre_ro_property(const char * name, const TProperty* property) {
    return FibreProperty<const TProperty>(name, property);
};

// Non-const enum types
template<typename TProperty, ENABLE_IF(std::is_enum<TProperty>::value)>
FibreProperty<std::underlying_type_t<TProperty>> make_fibre_property(const char * name, TProperty* property) {
    return FibreProperty<std::underlying_type_t<TProperty>>(name, reinterpret_cast<std::underlying_type_t<TProperty>*>(property));
};

// Const enum types
template<typename TProperty, ENABLE_IF(std::is_enum<TProperty>::value)>
FibreProperty<const std::underlying_type_t<TProperty>> make_fibre_ro_property(const char * name, const TProperty* property) {
    return FibreProperty<const std::underlying_type_t<TProperty>>(name, reinterpret_cast<const std::underlying_type_t<TProperty>*>(property));
};


template<typename ... TArgs>
struct PropertyListFactory;

template<>
struct PropertyListFactory<> {
    template<unsigned IPos, typename ... TAllProperties>
    static MemberList<> make_property_list(std::array<const char *, sizeof...(TAllProperties)> names, std::tuple<TAllProperties...>& values) {
        return MemberList<>();
    }
};

template<typename TProperty, typename ... TProperties>
struct PropertyListFactory<TProperty, TProperties...> {
    template<unsigned IPos, typename ... TAllProperties>
    static MemberList<FibreProperty<TProperty>, FibreProperty<TProperties>...>
    make_property_list(std::array<const char *, sizeof...(TAllProperties)> names, std::tuple<TAllProperties...>& values) {
        return MemberList<FibreProperty<TProperty>, FibreProperty<TProperties>...>(
            make_fibre_property(std::get<IPos>(names), &std::get<IPos>(values)),
            PropertyListFactory<TProperties...>::template make_property_list<IPos+1>(names, values)
        );
    }
};


template<typename TObj, typename ... TInputsAndOutputs>
class FibreFunction;

template<typename TObj, typename ... TInputs, typename ... TOutputs>
class FibreFunction<TObj, std::tuple<TInputs...>, std::tuple<TOutputs...>> : Endpoint {
public:
    // @brief The return type of the function as written by a C++ programmer
    using TRet = typename return_type<TOutputs...>::type;

    static constexpr size_t endpoint_count = 1 + MemberList<FibreProperty<TInputs>...>::endpoint_count + MemberList<FibreProperty<TOutputs>...>::endpoint_count;

    FibreFunction(const char * name, TObj& obj, TRet(TObj::*func_ptr)(TInputs...),
            std::array<const char *, sizeof...(TInputs)> input_names,
            std::array<const char *, sizeof...(TOutputs)> output_names) :
        name_(name), obj_(&obj), func_ptr_(func_ptr),
        input_names_{input_names}, output_names_{output_names},
        input_properties_(PropertyListFactory<TInputs...>::template make_property_list<0>(input_names_, in_args_)),
        output_properties_(PropertyListFactory<TOutputs...>::template make_property_list<0>(output_names_, out_args_))
    {
        LOG_FIBRE("my tuple is at %x and of size %u\r\n", (uintptr_t)&in_args_, sizeof(in_args_));
    }

    // The custom copy constructor is needed because otherwise the
    // input_properties_ and output_properties_ would point to memory
    // locations of the old object.
    FibreFunction(const FibreFunction& other) :
        name_(other.name_), obj_(other.obj_), func_ptr_(other.func_ptr_),
        input_names_{other.input_names_}, output_names_{other.output_names_},
        input_properties_(PropertyListFactory<TInputs...>::template make_property_list<0>(input_names_, in_args_)),
        output_properties_(PropertyListFactory<TOutputs...>::template make_property_list<0>(output_names_, out_args_))
    {
        LOG_FIBRE("COPIED! my tuple is at %x and of size %u\r\n", (uintptr_t)&in_args_, sizeof(in_args_));
    }

    void write_json(size_t id, fibre::StreamSink* output) {
        // write name
        write_string("{\"name\":\"", output);
        write_string(name_, output);

        // write endpoint ID
        write_string("\",\"id\":", output);
        char id_buf[10];
        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)id); // TODO: get rid of printf
        write_string(id_buf, output);
        
        // write arguments
        write_string(",\"type\":\"function\",\"inputs\":[", output);
        input_properties_.write_json(id + 1, output),
        write_string("],\"outputs\":[", output);
        output_properties_.write_json(id + 1 + decltype(input_properties_)::endpoint_count, output),
        write_string("]}", output);
    }

    // special-purpose function - to be moved
    Endpoint* get_by_name(const char * name, size_t length) {
        return nullptr; // can't address functions by name
    }

    void register_endpoints(Endpoint** list, size_t id, size_t length) {
        if (id < length)
            list[id] = this;
        input_properties_.register_endpoints(list, id + 1, length);
        output_properties_.register_endpoints(list, id + 1 + decltype(input_properties_)::endpoint_count, length);
    }

    template<typename> std::enable_if_t<sizeof...(TOutputs) == 0>
    handle_ex() {
        invoke_function_with_tuple(*obj_, func_ptr_, in_args_);
    }

    template<typename> std::enable_if_t<sizeof...(TOutputs) == 1>
    handle_ex() {
        std::get<0>(out_args_) = invoke_function_with_tuple(*obj_, func_ptr_, in_args_);
    }
    
    template<typename> std::enable_if_t<sizeof...(TOutputs) >= 2>
    handle_ex() {
        out_args_ = invoke_function_with_tuple(*obj_, func_ptr_, in_args_);
    }

    void handle(const uint8_t* input, size_t input_length, fibre::StreamSink* output) final {
        (void) input;
        (void) input_length;
        (void) output;
        LOG_FIBRE("tuple still at %x and of size %u\r\n", (uintptr_t)&in_args_, sizeof(in_args_));
        LOG_FIBRE("invoke function using %d and %.3f\r\n", std::get<0>(in_args_), std::get<1>(in_args_));
        handle_ex<void>();
    }

    const char * name_;
    TObj* obj_;
    TRet(TObj::*func_ptr_)(TInputs...);
    std::array<const char *, sizeof...(TInputs)> input_names_; // TODO: remove
    std::array<const char *, sizeof...(TOutputs)> output_names_; // TODO: remove
    std::tuple<TInputs...> in_args_;
    std::tuple<TOutputs...> out_args_;
    MemberList<FibreProperty<TInputs>...> input_properties_;
    MemberList<FibreProperty<TOutputs>...> output_properties_;
};

template<typename TObj, typename ... TArgs, typename ... TNames,
        typename = std::enable_if_t<sizeof...(TArgs) == sizeof...(TNames)>>
FibreFunction<TObj, std::tuple<TArgs...>, std::tuple<>> make_fibre_function(const char * name, TObj& obj, void(TObj::*func_ptr)(TArgs...), TNames ... names) {
    return FibreFunction<TObj, std::tuple<TArgs...>, std::tuple<>>(name, obj, func_ptr, {names...}, {});
}

template<typename TObj, typename TRet, typename ... TArgs, typename ... TNames,
        typename = std::enable_if_t<sizeof...(TArgs) == sizeof...(TNames) && !std::is_void<TRet>::value>>
FibreFunction<TObj, std::tuple<TArgs...>, std::tuple<TRet>> make_fibre_function(const char * name, TObj& obj, TRet(TObj::*func_ptr)(TArgs...), TNames ... names) {
    return FibreFunction<TObj, std::tuple<TArgs...>, std::tuple<TRet>>(name, obj, func_ptr, {names...}, {"result"});
}


#define FIBRE_EXPORTS(CLASS, ...) \
    struct fibre_export_t { \
        static CLASS* obj; \
        using type = decltype(make_fibre_member_list(__VA_ARGS__)); \
    }; \
    typename fibre_export_t::type make_fibre_definitions() { \
        CLASS* obj = this; \
        return make_fibre_member_list(__VA_ARGS__); \
    } \
    typename fibre_export_t::type fibre_definitions = make_fibre_definitions()





class EndpointProvider {
public:
    virtual size_t get_endpoint_count() = 0;
    virtual void write_json(size_t id, fibre::StreamSink* output) = 0;
    virtual Endpoint* get_by_name(char * name, size_t length) = 0;
    virtual void register_endpoints(Endpoint** list, size_t id, size_t length) = 0;
};

template<typename T>
class EndpointProvider_from_MemberList : public EndpointProvider {
public:
    EndpointProvider_from_MemberList(T& member_list) : member_list_(member_list) {}
    size_t get_endpoint_count() final {
        return T::endpoint_count;
    }
    void write_json(size_t id, fibre::StreamSink* output) final {
        return member_list_.write_json(id, output);
    }
    void register_endpoints(Endpoint** list, size_t id, size_t length) final {
        return member_list_.register_endpoints(list, id, length);
    }
    Endpoint* get_by_name(char * name, size_t length) final {
        for (size_t i = 0; i < length; i++) {
            if (name[i] == '.')
                name[i] = 0;
        }
        name[length-1] = 0;
        return member_list_.get_by_name(name, length);
    }
    T& member_list_;
};



class JSONDescriptorEndpoint : Endpoint {
public:
    static constexpr size_t endpoint_count = 1;
    void write_json(size_t id, fibre::StreamSink* output);
    void register_endpoints(Endpoint** list, size_t id, size_t length);
    void handle(const uint8_t* input, size_t input_length, fibre::StreamSink* output);
};

#include "types.hpp"

// defined in protocol.cpp
extern Endpoint** endpoint_list_;
extern size_t n_endpoints_;
extern uint16_t json_crc_;
extern JSONDescriptorEndpoint json_file_endpoint_;
extern EndpointProvider* application_endpoints_;

namespace fibre {

class LocalEndpoint;
class OutputPipe;

class IncomingConnectionDecoder : public DynamicStreamChain<RX_BUF_SIZE - 52> {
public:
    IncomingConnectionDecoder(OutputPipe& output_pipe) : output_pipe_(&output_pipe) {
        set_stream<HeaderDecoderChain>(FixedIntDecoder<uint16_t, false>(), FixedIntDecoder<uint16_t, false>());
    }
    template<typename TDecoder, typename ... TArgs>
    void set_stream(TArgs&& ... args) {
        DynamicStreamChain::set_stream<TDecoder, TArgs...>(std::forward<TArgs>(args)...);
    }
    void set_stream(StreamSink* new_stream) {
        DynamicStreamChain::set_stream(new_stream);
    }
    template<typename TDecoder>
    TDecoder* get_stream() {
        return DynamicStreamChain::get_stream<TDecoder>();
    }
    template<typename TDecoder>
    const TDecoder* get_stream() const {
        return DynamicStreamChain::get_stream<TDecoder>();
    }
private:
    enum {
        RECEIVING_HEADER,
        RECEIVING_PAYLOAD
    } state_ = RECEIVING_HEADER;
    LocalEndpoint* endpoint_ = nullptr;
    OutputPipe* output_pipe_ = nullptr;

    using HeaderDecoderChain = StaticStreamChain<
                FixedIntDecoder<uint16_t, false>,
                FixedIntDecoder<uint16_t, false>
            >; // size: 96 bytes

    status_t advance_state() final;
};

class LocalEndpoint {
public:
    //virtual void invoke(StreamSource* input, StreamSink* output) = 0;
    virtual void open_connection(IncomingConnectionDecoder& input) = 0;
    virtual void decoder_finished(const IncomingConnectionDecoder& input, OutputPipe* output) = 0;
    virtual uint16_t get_hash() = 0;
    virtual bool get_as_json(char output[256] /* TODO: use a stream as output instead */) = 0;
};


/*
template<typename T>
T deserialize(StreamSource& input);

template<>
uint32_t deserialize<uint32_t>(StreamSource& input) {
    return 0;
}

template<typename ... Ts>
std::tuple<Ts...> deserialize<std::tuple<Ts...>>(StreamSource& input) {
    return std::make_tuple<Ts...>(deserialize<Ts>(input)...);
}*/

/*
template<typename T>
struct Deserializer;

template<>
struct Deserializer<uint32_t> {
    static uint32_t deserialize(StreamSource* input) {
        uint8_t buf[4];
        input->get_bytes(buf, sizeof(buf), nullptr);
        return read_le<uint32_t>(buf);
    }
};

template<typename ... Ts>
struct Deserializer<std::tuple<Ts...>> {
    static std::tuple<Ts...> deserialize(StreamSource* input) {
        return std::make_tuple<Ts...>(Deserializer<Ts>::deserialize(input)...);
    }
};*/


template<typename T>
struct Serializer;

template<>
struct Serializer<uint32_t> {
    static void serialize(uint32_t& value, StreamSink* output) {
        uint8_t buf[4];
        write_le<uint32_t>(value, buf);
        output->process_bytes(buf, sizeof(buf), nullptr);
    }
};

template<size_t I>
struct Serializer<char[I]> {
    static void serialize(char (&value)[I], StreamSink* output) {
        uint32_t i = I;
        Serializer<uint32_t>::serialize(i, output);
        LOG_FIBRE("will write string len %zu", I);
        size_t processed_bytes = 0;
        StreamSink::status_t status = output->process_bytes(reinterpret_cast<uint8_t*>(value), I, &processed_bytes);
        if (processed_bytes != I) {
            LOG_FIBRE("not everything processed: %zu", processed_bytes);
        }
        LOG_FIBRE("status %d", status);
        hexdump(reinterpret_cast<uint8_t*>(value), I);
    }
};

template<typename ... Ts>
struct Serializer<std::tuple<Ts...>> {
    static void serialize(std::tuple<Ts...>& value, StreamSink* output) {
        //Serializer<std::tuple<T, Ts...>>::serialize
        serialize_tail<sizeof...(Ts)>(value, output);
    }

    template<size_t I>
    static std::enable_if_t<I >= 1>
    serialize_tail(std::tuple<Ts...>& value, StreamSink* output) {
        Serializer<std::tuple_element_t<I - 1, std::tuple<Ts...>>>::serialize(std::get<I - 1>(value), output);
        serialize_tail<I - 1>(value, output);
    }
    
    template<size_t I>
    static std::enable_if_t<I == 0>
    serialize_tail(std::tuple<Ts...>& value, StreamSink* output) {
        // nothing to do
    }
};

//template<unsigned IPos, typename Ts...>
//void deserialize_tuple(std::tuple<Ts...>& values) {
//    //using TCurrent = decltype(std::get<IPos>(values));
//    //std::get<0>(values) = deserialize<TCurrent>();
//    //deserialize_tuple<TPos+1>(values);
//}

/*
template<typename... TNames>
struct FunctionJSONAssembler;

template<typename TFunctionName, typename... TInputNames, typename... TOutputNames>
struct FunctionJSONAssembler<TFunctionName, std::tuple<TInputNames...>, std::tuple<TOutputNames...>> {
    template<TFunctionName& function_name, const std::tuple<TInputNames...>& input_names, const std::tuple<TOutputNames...>& output_names>
    class WithStaticNames {
        template<size_t I>
        static constexpr array_string<10 + sizeof(std::get<I+1>(input_names))/sizeof(std::get<I+1>(input_names)[0])>
        get_input_json() {
            return const_str_concat(
                make_const_string("{\"name\":\""),
                make_const_string(std::get<I+1>(input_names)),
                make_const_string("\"}")
            );
        }

        template<size_t... Is>
        static constexpr auto get_all_inputs_json(std::index_sequence<Is...>)
            -> decltype(const_str_join(get_input_json<Is>()...)) {
            return const_str_join(get_input_json<Is>()...);
        }

        // @brief Returns a JSON snippet that describes this function
        static bool get_as_json(char output[256]) {
            static const constexpr auto json = const_str_concat(
                make_const_string("{\"name\":\""),
                make_const_string(function_name),
                make_const_string("\",\"in\":["),
                get_all_inputs_json(std::make_index_sequence<sizeof...(TInputNames)>()),
                make_const_string("]}")
            );
            memcpy(output, json.c_str(), std::min((size_t)256, sizeof(json)));
            return true;
        }
    };
};*/



template<typename TFunctionProperties>
struct FunctionJSONAssembler {
    template<size_t I>
    static constexpr array_string<10 + sizeof(std::get<I>(TFunctionProperties::get_input_names()))/sizeof(std::get<I>(TFunctionProperties::get_input_names())[0])>
    get_input_json() {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            make_const_string(std::get<I>(TFunctionProperties::get_input_names())),
            make_const_string("\"}")
        );
    }

    template<size_t... Is>
    static constexpr auto get_all_inputs_json(std::index_sequence<Is...>)
        -> decltype(const_str_join(get_input_json<Is>()...)) {
        return const_str_join(get_input_json<Is>()...);
    }

    // @brief Returns a JSON snippet that describes this function
    static bool get_as_json(char output[256]) {
        static const constexpr auto json = const_str_concat(
            make_const_string("{\"name\":\""),
            make_const_string(TFunctionProperties::get_function_name()),
            make_const_string("\",\"in\":["),
            get_all_inputs_json(std::make_index_sequence<TFunctionProperties::NInputs>()),
            make_const_string("]}")
        );
        memcpy(output, json.c_str(), std::min((size_t)256, sizeof(json)));
        return true;
    }
};

template<typename TFun, typename TIn, typename TOut>
struct StaticFunctionProperties;

template<size_t IFun, size_t... IIn, size_t... IOut>
struct StaticFunctionProperties<const char [IFun], std::tuple<const char(&)[IIn]...>, std::tuple<const char(&)[IOut]...>> {
    template<
        const char (&function_name)[IFun],
        const std::tuple<const char(&)[IIn]...>& input_names,
        const std::tuple<const char(&)[IOut]...>& output_names
    >
    struct WithStaticNames {
        using TFun = const char (&)[IFun];
        static constexpr const size_t NInputs = (sizeof...(IIn));
        static constexpr const size_t NOutputs = (sizeof...(IOut));
        static constexpr TFun get_function_name() { return function_name; }
        static constexpr const std::tuple<const char(&)[IIn]...>& get_input_names() { return input_names; }
        static constexpr const std::tuple<const char(&)[IOut]...>& get_output_names() { return output_names; }
    };
};

/*template<size_t... IIn>
auto make_function_properties(std::tuple<const char(&)[IIn]...> input_names)
    -> typename StaticFunctionProperties<IIn...>::WithStaticNames<input_names>
{
    return StaticFunctionProperties<IIn>::WithStaticNames<input_names>();
}*/

struct get_value_functor {
    template<typename T>
    decltype(std::declval<T>().get_value()) operator () (T&& t) {
        return t.get_value();
    }
};

template<typename ... TInputsAndOutputs>
struct FunctionStuff;

/*
* C++ doesn't seem to allow to pass a function pointer with a generic signature
* as a template argument. Therefore we need to employ nesting of two templated types.
*/
template<typename ... TInputs, typename ... TOutputs, typename TFunctionProperties>
struct FunctionStuff<std::tuple<TInputs...>, std::tuple<TOutputs...>, TFunctionProperties> {
    static constexpr const size_t NInputs = (sizeof...(TInputs));
    static constexpr const size_t NOutputs = (sizeof...(TOutputs));
    //static_assert(NInputs + NOutputs + 1 == sizeof...(TNames),
    //              "a type must be provided for the function name and each input and output name");
    static_assert(NInputs == TFunctionProperties::NInputs);
    static_assert(NOutputs == TFunctionProperties::NOutputs);
    using TRet = typename return_type<TOutputs...>::type;

/*        template<TRet(*Function)(TInputs...)>
        class WithStaticFuncPtr : public LocalEndpoint {
            void invoke(StreamSource* input, StreamSink* output) final {
                std::tuple<TInputs...> inputs = Deserializer<std::tuple<TInputs...>>::deserialize(input);
                std::tuple<TOutputs...> outputs =
                    static_function_traits<std::tuple<TInputs...>, std::tuple<TOutputs...>>::template invoke<Function>(inputs);
                Serializer<std::tuple<TOutputs...>>::serialize(outputs, output);
            }

            // @brief Returns a JSON snippet that describes this function
            bool get_as_json(char output[256]) final {
                return WithStaticNames::get_as_json(output);
            }
        };*/

    template<bool(*Function)(TInputs..., TOutputs&...)>
    class WithStaticFuncPtr2 : public LocalEndpoint {
        using decoder_type = StaticStreamChain<FixedIntDecoder<TInputs, false>...>;
        void open_connection(IncomingConnectionDecoder& incoming_connection_decoder) final {
            incoming_connection_decoder.set_stream<decoder_type>(FixedIntDecoder<TInputs, false>()...);
        }
        void decoder_finished(const IncomingConnectionDecoder& incoming_connection_decoder, OutputPipe* output) final {
            const decoder_type* decoder = incoming_connection_decoder.get_stream<decoder_type>();
            printf("arg decoder finished\n");
            using tuple_type = std::tuple<FixedIntDecoder<TInputs, false>...>;
            std::tuple<const TInputs&...> inputs = for_each_in_tuple(get_value_functor(), std::forward<const tuple_type>(decoder->get_all_streams()));
            std::tuple<TOutputs...> outputs;
            //std::tuple<TOutputs&...> output_refs = std::forward_as_tuple(outputs);
            std::tuple<TOutputs&...> output_refs(std::get<0>(outputs));
            std::tuple<const TInputs&..., TOutputs&...> in_and_out = std::tuple_cat(inputs, output_refs);
            std::apply(Function, in_and_out);
            printf("first arg is %08x\n", std::get<0>(inputs));
            LOG_FIBRE("will write output of type %s", typeid(TOutputs).name()...);
            Serializer<std::tuple<TOutputs...>>::serialize(outputs, output);
        }

        /*void invoke(StreamSource* input, StreamSink* output) final {
            fprintf(stderr, "about to prep endpoint\n");
            std::tuple<TInputs...> inputs = Deserializer<std::tuple<TInputs...>>::deserialize(input);
            std::tuple<TOutputs...> outputs;
            //std::tuple<TOutputs&...> output_refs = std::forward_as_tuple(outputs);
            std::tuple<TOutputs&...> output_refs(std::get<0>(outputs));
            std::tuple<TInputs..., TOutputs&...> in_and_out = std::tuple_cat(inputs, output_refs);
            fprintf(stderr, "about to invoke endpoint\n");
            static_function_traits<std::tuple<TInputs..., TOutputs&...>, std::tuple<bool>>::template invoke<Function>(in_and_out);
            Serializer<std::tuple<TOutputs...>>::serialize(outputs, output);
        }*/

        uint16_t get_hash() {
            // TODO: implement
            return 0;
        }

        // @brief Returns a JSON snippet that describes this function
        bool get_as_json(char output[256]) final {
            return FunctionJSONAssembler<TFunctionProperties>::get_as_json(output);
        }
    };
};



class InputPipe {
    // don't allow copy/move (we don't know if it's save to relocate the buffer)
    InputPipe(const InputPipe&) = delete;
    InputPipe& operator=(const InputPipe&) = delete;

    // TODO: use Decoder infrastructure to process incoming bytes
    uint8_t rx_buf_[RX_BUF_SIZE];
    uint8_t tx_buf_[TX_BUF_SIZE]; // TODO: this does not belong here
    size_t pos_ = 0;
    size_t crc_ = CANONICAL_CRC16_INIT;
    size_t total_length_ = 0;
    bool total_length_known = false;
    size_t id_;
    StreamSink* input_handler = nullptr; // TODO: destructor
public:
    InputPipe(size_t id) : id_(id) {}

    template<typename TDecoder, typename ... TArgs>
    void construct_decoder(TArgs&& ... args) {
        static_assert(sizeof(TDecoder) <= RX_BUF_SIZE, "TDecoder is too large. Increase the buffer size of this pipe.");
        input_handler = new (rx_buf_) TDecoder(std::forward<TArgs>(args)...);
    }

    void process_chunk(const uint8_t* buffer, size_t offset, size_t length, uint16_t crc, bool close_pipe) {
        if (offset > pos_) {
            LOG_FIBRE("disjoint chunk reassembly not implemented");
            // TODO: implement disjoint chunk reassembly
            return;
        }
        if (offset + length <= pos_) {
            LOG_FIBRE("duplicate data received");
            return;
        }
        // dump the beginning of the chunk if it's already known
        if (offset < pos_) {
            size_t diff = pos_ - offset;
            crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc, buffer, diff);
            buffer += diff;
            offset += diff;
            length -= diff;
        }
        if (crc != crc_) {
            LOG_FIBRE("received dangling chunk: expected CRC %04zx but got %04x", crc_, crc);
            return;
        }
        //if (offset + length > RX_BUF_SIZE) {
        //    LOG_FIBRE("input buffer overrun");
        //    return;
        //}
        //memcpy(rx_buf_ + offset, buffer, length);
        input_handler->process_bytes(buffer, length, nullptr /* TODO: why? */);
        pos_ = offset + length;
        // TODO: acknowledge received bytes
        if (close_pipe) {
            close();
        }
    }
    void close() {
        LOG_FIBRE("close pipe not implemented");
    }
    void packet_reset() {
        pos_ = 0;
        crc_ = CANONICAL_CRC16_INIT;
    }
};

class RemoteNode;

/**
 * @brief Represents a pipe into which the local node can pump data to send it
 * to the corresponding remote node's input pipe.
 * 
 * An output pipe optionally keep track of the chunks of data that were not yet
 * acknowledged.
 */
class OutputPipe final : public StreamSink {
    /*
    * For now we say that the probability of successful delivery is monotonically
    * decreasing with increasing stream offset.
    * This means if a chunk is acknowledged before all of its preceeding bytes
    * are acknogledged, we simply ignore this.
    */

    RemoteNode* remote_node_;
    uint8_t buffer_[TX_BUF_SIZE];
    size_t buffer_pos_ = 0; /** write position relative to the buffer start */
    size_t pipe_pos_ = 0; /** position of the beginning of the buffer within the byte stream */
    uint16_t crc_init_ = CANONICAL_CRC16_INIT;
    monotonic_time_t next_due_time_ = monotonic_time_t::min();
    size_t id_;
public:
    bool guaranteed_delivery = false;

    OutputPipe(RemoteNode* remote_node, size_t id) :
        remote_node_(remote_node),
        id_(id) { }

    size_t get_id() const { return id_; }

    class chunk_t {
    public:
        chunk_t(OutputPipe *pipe) : pipe_(pipe) {}
        bool get_properties(size_t* offset, size_t* length, uint16_t* crc_init) {
            if (offset) *offset = pipe_ ? pipe_->pipe_pos_ : 0;
            if (length) *length = pipe_ ? pipe_->buffer_pos_ : 0;
            if (crc_init) *crc_init = pipe_ ? pipe_->crc_init_ : 0;
            return true;
        }
        bool write_to(StreamSink* output, size_t length) {
            if (length > pipe_->buffer_pos_)
                return false;
            size_t processed_bytes = 0;
            status_t status = output->process_bytes(pipe_->buffer_, pipe_->buffer_pos_, &processed_bytes);
            if (processed_bytes != length)
                return false;
            return status != ERROR;
        }
    private:
        OutputPipe *pipe_;
    };

    class chunk_list {
    public:
        chunk_list(OutputPipe* pipe) : pipe_(pipe) {}
        //chunk_t operator[] (size_t index);
        chunk_t operator[] (size_t index) {
            if (!pipe_) {
                return { 0 }; // TODO: empty iterator
            } else if (index == 0) {
                return chunk_t(pipe_);
            } else {
                return { 0 };
            }
        }

        using iterator = simple_iterator<chunk_list, chunk_t>;
        iterator begin() { return iterator(*this, 0); }
        iterator end() { return iterator(*this, pipe_->buffer_pos_ ? 1 : 0); }
    private:
        OutputPipe *pipe_;
    };

    status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) final;

    //size_t get_n_due_bytes
    void get_available_non_blocking_bytes(size_t* offset, size_t* length, uint16_t* crc) {
        
    }

    chunk_list get_pending_chunks() {
        return chunk_list(this);
    }

    void drop_chunk(size_t offset, size_t length) {
        if (offset > pipe_pos_) {
            LOG_FIBRE("attempt to drop chunk at 0x%08zx but there's pending data before that at 0x%08zx", offset, pipe_pos_);
            return;
        }
        if (offset + length <= pipe_pos_) {
            LOG_FIBRE("already acknowledged");
            return;
        }
        if (offset < pipe_pos_) {
            offset += (pipe_pos_ - offset);
            length -= (pipe_pos_ - offset);
        }
        if (length > buffer_pos_) {
            LOG_FIBRE("ackowledged bytes that werent even available");
            return;
        }

        // shift buffer
        crc_init_ = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc_init_, buffer_, length);
        memmove(buffer_, buffer_ + length, buffer_pos_ - length);
        pipe_pos_ += length;
        buffer_pos_ = 0;
    }

    monotonic_time_t get_due_time() {
        return next_due_time_;
    }
    void set_due_time(size_t offset, size_t length, monotonic_time_t next_due_time) {
        // TODO: set due time for specific chunks
        next_due_time_ = next_due_time;
    }
};

/*namespace std {
    template<>
    void begin)=
}*/

/*OutputPipe::chunk_t OutputPipe::chunk_iterator::operator[] (size_t index) {
    if (index == 0) {
        return {
            .offset = pipe_->pipe_pos_,
            .crc = pipe_->crc_,
            .ptr = pipe_->buffer_,
            .length = pipe_->buffer_pos_,
        };
    } else {
        return { 0 };
    }
};*/

class OutputChannel : public StreamSink {
public:
    std::chrono::duration<uint32_t, std::milli> resend_interval = std::chrono::milliseconds(100);
    //StreamSink operator & () { return }
    //virtual StreamSink* get_stream_sink() = 0;
};


//template<typename TStream>
class OutputChannelFromStream : public OutputChannel {
    StreamSink* output_stream_;
public:
    OutputChannelFromStream(StreamSink* stream) : output_stream_(stream) {}
    //operator TStream&() const { return output_stream_; }
    //StreamSink* get_stream_sink() { return output_stream_; }
    //const StreamSink* get_stream_sink() const { return output_stream_; }
    status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) final {
        return output_stream_->process_bytes(buffer, length, processed_bytes);
    }
    size_t get_min_useful_bytes() const final {
        return output_stream_->get_min_useful_bytes();
    }
    size_t get_min_non_blocking_bytes() const final {
        return output_stream_->get_min_non_blocking_bytes();
    }
};


class RemoteNode {
public:
    RemoteNode(Uuid uuid) : uuid_(uuid) {}
    InputPipe* get_input_pipe(size_t id, bool* is_new);
    OutputPipe* get_output_pipe(size_t id);

    void schedule();

    void notify_output_pipe_ready();

    void notify_output_channel_ready();

    void add_output_channel(OutputChannel* channel) {
        output_channels_.push_back(channel);
    }
    void remove_output_channel(OutputChannel* channel) {
        output_channels_.erase(std::remove(output_channels_.begin(), output_channels_.end(), channel), output_channels_.end());
    }
private:
    std::unordered_map<size_t, InputPipe> client_input_pipes_;
    std::unordered_map<size_t, InputPipe> server_input_pipes_;
    std::unordered_map<size_t, OutputPipe> client_output_pipes_;
    std::unordered_map<size_t, OutputPipe> server_output_pipes_;
    std::vector<OutputChannel*> output_channels_;
    Uuid uuid_;

#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_PER_NODE_THREAD
    AutoResetEvent output_pipe_ready_;
    AutoResetEvent output_channel_ready_;
    
    void scheduler_loop() {
        for (;;) {
            output_pipes_ready_.wait();
            output_channels_ready_.wait();
            schedule();
        }
    }
#endif
};

//class InputChannel {
//public:
//    // TODO: use Decoder infrastructure to process incoming bytes
//    int process_packet(RemoteNode& origin, const uint8_t* buffer, size_t length);
//private:
//    //InputPipe pipes_[N_PIPES];
//};


class InputChannelDecoder : public StreamSink {
public:
    InputChannelDecoder(RemoteNode* remote_node) :
        remote_node_(remote_node),
        header_decoder_(make_header_decoder())
        {}
    
    status_t process_bytes(const uint8_t* buffer, size_t length, size_t *processed_bytes) final {
        while (length) {
            size_t chunk = 0;
            if (in_header) {
                status_t status = header_decoder_.process_bytes(buffer, length, &chunk);

                buffer += chunk;
                length -= chunk;
                if (processed_bytes)
                    *processed_bytes += chunk;

                // finished receiving chunk header
                if (status == CLOSED) {
                    LOG_FIBRE("received chunk header: pipe %04x, offset %04x, length %04x, crc %04x", get_pipe_no(), get_chunk_offset(), get_chunk_length(), get_chunk_crc());
                    in_header = false;
                    bool is_new = false;
                    uint16_t pipe_no = get_pipe_no();
                    input_pipe_ = remote_node_->get_input_pipe(pipe_no, &is_new);
                    if (!input_pipe_) {
                        LOG_FIBRE("no pipe %d associated with this source", pipe_no);
                        //reset();
                        continue;
                    }
                    if (is_new) {
                        OutputPipe* output_pipe = remote_node_->get_output_pipe(pipe_no & 0x8000);
                        input_pipe_->construct_decoder<IncomingConnectionDecoder>(*output_pipe);
                    }
                }
            } else {
                uint16_t& chunk_offset = get_chunk_offset();
                uint16_t& chunk_length = get_chunk_length();
                uint16_t& chunk_crc = get_chunk_crc();

                size_t actual_length = std::min(static_cast<size_t>(chunk_length), length);
                if (input_pipe_)
                    input_pipe_->process_chunk(buffer, get_chunk_offset(), actual_length, get_chunk_crc(), false);
                //status_t status = input_pipe_.process_bytes(buffer, std::min(length, remaining_payload_bytes_), &chunk);

                chunk_crc = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(chunk_crc, buffer, actual_length);
                buffer += actual_length;
                length -= actual_length;
                chunk_offset += actual_length;
                chunk_length -= actual_length;

                if (processed_bytes)
                    *processed_bytes += actual_length;

                if (!chunk_length) {
                    reset();
                }
            }
        }
        return OK;
    }
private:
    using HeaderDecoder = StaticStreamChain<
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>,
            FixedIntDecoder<uint16_t, false>>;
    RemoteNode* remote_node_;
    InputPipe* input_pipe_;
    HeaderDecoder header_decoder_;
    bool in_header = true;

    static HeaderDecoder make_header_decoder() {
        return HeaderDecoder(
                FixedIntDecoder<uint16_t, false>(),
                FixedIntDecoder<uint16_t, false>(),
                FixedIntDecoder<uint16_t, false>(),
                FixedIntDecoder<uint16_t, false>()
        );
    }

    uint16_t& get_pipe_no() {
        return header_decoder_.get_stream<0>().get_value();
    }
    uint16_t& get_chunk_offset() {
        return header_decoder_.get_stream<1>().get_value();
    }
    uint16_t& get_chunk_crc() {
        return header_decoder_.get_stream<2>().get_value();
    }
    uint16_t& get_chunk_length() {
        return header_decoder_.get_stream<3>().get_value();
    }

    void reset() {
        input_pipe_ = nullptr;
        header_decoder_ = make_header_decoder();
        in_header = true;
    }
};


void init();
void publish_function(LocalEndpoint* function);
void publish_ref_type(FibreRefType* type);
RemoteNode* get_remote_node(Uuid uuid);
// TODO: use Decoder infrastructure to process incoming bytes
//bool process_bytes(RemoteNode* origin, const uint8_t *buffer, size_t length);
//bool process_packet(RemoteNode* origin, const uint8_t* buffer, size_t length);




// @brief Registers the specified application object list using the provided endpoint table.
// This function should only be called once during the lifetime of the application. TODO: fix this.
// @param application_objects The application objects to be registred.
template<typename T>
int publish_object(T& application_objects) {
//    static constexpr size_t endpoint_list_size = 1 + T::endpoint_count;
//    static Endpoint* endpoint_list[endpoint_list_size];
//    static auto endpoint_provider = EndpointProvider_from_MemberList<T>(application_objects);
    using ref_type = ::FibreRefType<T>;
    ref_type& asd = fibre::global_instance_of<ref_type>();
    //ref_types_.push_back(fibre::global_instance_of<ref_type>());
    //fibre::FibreRefType& aaa = asd;
    publish_ref_type(&asd);
/*    json_file_endpoint_.register_endpoints(endpoint_list, 0, endpoint_list_size);
    application_objects.register_endpoints(endpoint_list, 1, endpoint_list_size);

    // Update the global endpoint table
    endpoint_list_ = endpoint_list;
    n_endpoints_ = endpoint_list_size;
    application_endpoints_ = &endpoint_provider;
    
    // Calculate the CRC16 of the JSON file.
    // The init value is the protocol version.
    CRC16Calculator crc16_calculator(PROTOCOL_VERSION);
    uint8_t offset[4] = { 0 };
    json_file_endpoint_.handle(offset, sizeof(offset), &crc16_calculator);
    json_crc_ = crc16_calculator.get_crc16();
*/
    return 0;
}


} // namespace fibre

#endif
