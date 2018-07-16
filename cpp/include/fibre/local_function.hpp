#ifndef __FIBRE_LOCAL_ENDPOINT
#define __FIBRE_LOCAL_ENDPOINT

#ifndef __FIBRE_HPP
#error "This file should not be included directly. Include fibre.hpp instead."
#endif

namespace fibre {

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
        LOG_FIBRE(SERDES, "will write string len ", I);
        size_t processed_bytes = 0;
        StreamSink::status_t status = output->process_bytes(reinterpret_cast<uint8_t*>(value), I, &processed_bytes);
        if (processed_bytes != I) {
            LOG_FIBRE_W(SERDES, "not everything processed: ", processed_bytes);
        }
        //LOG_FIBRE(SERDES, "status %d", status);
        //hexdump(reinterpret_cast<uint8_t*>(value), I);
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
            //printf("arg decoder finished");
            using tuple_type = std::tuple<FixedIntDecoder<TInputs, false>...>;
            std::tuple<const TInputs&...> inputs = for_each_in_tuple(get_value_functor(), std::forward<const tuple_type>(decoder->get_all_streams()));
            std::tuple<TOutputs...> outputs;
            //std::tuple<TOutputs&...> output_refs = std::forward_as_tuple(outputs);
            std::tuple<TOutputs&...> output_refs(std::get<0>(outputs));
            std::tuple<const TInputs&..., TOutputs&...> in_and_out = std::tuple_cat(inputs, output_refs);
            std::apply(Function, in_and_out);
            //printf("first arg is ", std::hex, std::get<0>(inputs));
            LOG_FIBRE(SERDES, "will write output of type ", typeid(TOutputs).name()...);
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

}

#endif // __FIBRE_LOCAL_ENDPOINT
