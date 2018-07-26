#ifndef __FIBRE_LOCAL_ENDPOINT
#define __FIBRE_LOCAL_ENDPOINT

//#ifndef __FIBRE_HPP
//#error "This file should not be included directly. Include fibre.hpp instead."
//#endif
#include <fibre/fibre.hpp>

namespace fibre {

class LocalEndpoint {
public:
    //virtual void invoke(StreamSource* input, StreamSink* output) = 0;
    virtual void open_connection(IncomingConnectionDecoder& input) = 0;
    virtual void decoder_finished(const IncomingConnectionDecoder& input, OutputPipe* output) = 0;
    virtual uint16_t get_hash() = 0;
    virtual bool get_as_json(const char ** output, size_t* length) = 0;
};


/**
 * @brief Assembles a JSON snippet that describes a function
 */
template<typename TMetadata, TMetadata& metadata>
struct FunctionJSONAssembler {
    template<size_t I>
    //static constexpr static_string<10 + sizeof(std::get<I>(metadata.get_input_names()))/sizeof(std::get<I>(metadata.get_input_names())[0])>
    static constexpr static_string<11 + std::get<I>(metadata.get_input_names()).size()>
    get_input_json() {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            std::get<I>(metadata.get_input_names()),
            make_const_string("\"}")
        );
    }

    template<size_t... Is>
    static constexpr auto get_all_inputs_json(std::index_sequence<Is...>)
        -> decltype(const_str_join(std::declval<static_string<1>>(), get_input_json<Is>()...)) {
        return const_str_join(make_const_string(","), get_input_json<Is>()...);
    }

    // @brief Returns a JSON snippet that describes this function
    static bool get_as_json(const char ** output, size_t* length) {
        static const constexpr auto json = const_str_concat(
            make_const_string("{\"name\":\""),
            metadata.get_function_name(),
            make_const_string("\",\"in\":["),
            get_all_inputs_json(std::make_index_sequence<TMetadata::NInputs>()),
            make_const_string("]}")
        );
        //memcpy(output, json.c_str(), std::min((size_t)256, sizeof(json)));
        if (output)
            *output = json.c_str();
        if (length)
            *length = sizeof(json);
        return true;
    }
};

template<typename TFuncName, typename TInputNames, typename TOutputNames>
struct StaticFunctionProperties;

template<size_t IFun, size_t... IIn, size_t... IOut>
struct StaticFunctionProperties<
        static_string<IFun>,
        static_string_arr<IIn...>,
        static_string_arr<IOut...>> {
    using TFuncName = static_string<IFun>;
    using TInputNames = static_string_arr<IIn...>;
    using TOutputNames = static_string_arr<IOut...>;
    TFuncName function_name;
    TInputNames input_names;
    TOutputNames output_names;

    constexpr StaticFunctionProperties(TFuncName function_name, TInputNames input_names, TOutputNames output_names)
        : function_name(function_name), input_names(input_names), output_names(output_names) {}

    template<size_t ... Is>
    constexpr StaticFunctionProperties<TFuncName, static_string_arr<Is...>, TOutputNames> with_inputs(const char (&...names)[Is]) {
        return StaticFunctionProperties<TFuncName, static_string_arr<Is...>, TOutputNames>(
            function_name,
            std::tuple_cat(input_names, std::tuple<const char (&)[Is]...>(names...)),
            output_names);
    }

    template<size_t ... Is>
    constexpr StaticFunctionProperties<TFuncName, TInputNames, static_string_arr<Is...>> with_outputs(const char (&...names)[Is]) {
        return StaticFunctionProperties<TFuncName, TInputNames, static_string_arr<Is...>>(
            function_name,
            input_names,
            std::tuple_cat(output_names, std::tuple<const char (&)[Is]...>(names...)));
    }

    static constexpr const size_t NInputs = (sizeof...(IIn));
    static constexpr const size_t NOutputs = (sizeof...(IOut));
    constexpr TFuncName get_function_name() { return function_name; }
    constexpr TInputNames get_input_names() { return input_names; }
    constexpr TOutputNames get_output_names() { return output_names; }
};


template<size_t IFunc>
static constexpr StaticFunctionProperties<static_string<IFunc>, static_string_arr<>, static_string_arr<>> make_function_props(const char (&function_name)[IFunc]) {
    return StaticFunctionProperties<static_string<IFunc>, static_string_arr<>, static_string_arr<>>(static_string<IFunc>(function_name), empty_static_string_arr, empty_static_string_arr);
}

struct get_value_functor {
    template<typename T>
    decltype(std::declval<T>().get_value()) operator () (T&& t) {
        return t.get_value();
    }
};



template<size_t NInputs, typename... Ts>
struct decoder_type;

template<size_t NInputs>
struct decoder_type<NInputs> {
    static_assert(NInputs == 0, "too many input names provided");
    using type = std::tuple<>;
    using remainder = std::tuple<>;
};

template<typename T, typename... Ts>
struct decoder_type<0, T, Ts...> {
    using type = std::tuple<>;
    using remainder = std::tuple<T, Ts...>;
};

template<size_t NInputs, typename... TTuple, typename... Ts>
struct decoder_type<NInputs, std::tuple<TTuple...>, Ts...> {
private:
    using unpacked = decoder_type<NInputs, TTuple..., Ts...>;
public:
    using type = typename unpacked::type;
    using remainder = typename unpacked::remainder;
};

template<size_t NInputs, typename... Ts>
struct decoder_type<NInputs, uint32_t, Ts...> {
private:
    using tail = decoder_type<NInputs - 1, Ts...>;
    using this_type = FixedIntDecoder<uint32_t, false>;
public:
    using type = tuple_cat_t<std::tuple<this_type>, typename tail::type>;
    using remainder = typename tail::remainder;
};

template<size_t NOutputs, typename... Ts>
struct encoder_type;

template<size_t NOutputs>
struct encoder_type<NOutputs> {
    static_assert(NOutputs == 0, "too many output names provided");
    using type = std::tuple<>;
    using remainder = std::tuple<>;
    static void serialize(StreamSink* output) {
        // nothing to do
    }
};

template<typename T, typename... Ts>
struct encoder_type<0, T, Ts...> {
    using type = std::tuple<>;
    using remainder = std::tuple<T, Ts...>;
    static void serialize(StreamSink* output, T&& value, Ts&&... tail_values) {
        // nothing to do
    }
};

template<size_t NOutputs, typename... TTuple, typename... Ts>
struct encoder_type<NOutputs, std::tuple<TTuple...>, Ts...> {
private:
    using unpacked = encoder_type<NOutputs, TTuple..., Ts...>;
public:
    //using type = typename unpacked::type;
    //using remainder = typename unpacked::remainder;

    template<size_t... Is>
    static void serialize_impl(std::index_sequence<Is...>, StreamSink* output, std::tuple<TTuple...>&& val, Ts&&... tail_values) {
        unpacked::serialize(output, std::forward<TTuple>(std::get<Is>(val))..., std::forward<Ts>(tail_values)...);
    }
    static void serialize(StreamSink* output, std::tuple<TTuple...>&& val, Ts&&... tail_values) {
        serialize_impl(std::make_index_sequence<sizeof...(TTuple)>(), output, std::forward<std::tuple<TTuple...>>(val), std::forward<Ts>(tail_values)...);
    }
};

template<size_t NOutputs, typename... Ts>
struct encoder_type<NOutputs, uint32_t, Ts...> {
private:
    using tail = encoder_type<NOutputs - 1, Ts...>;
public:
    //using type = tuple_cat_t<std::tuple<this_type>, typename tail::type>;
    //using remainder = typename tail::remainder;

    static void serialize(StreamSink* output, uint32_t&& value, Ts&&... tail_values) {
        uint8_t buf[4];
        write_le<uint32_t>(value, buf);
        output->process_bytes(buf, sizeof(buf), nullptr);
        tail::serialize(output, std::forward<Ts>(tail_values)...);
    }
};

template<size_t NOutputs, typename... Ts>
struct encoder_type<NOutputs, const char *, size_t, Ts...> {
private:
    using tail = encoder_type<NOutputs - 1, Ts...>;
    using this_data_type = std::tuple<const char *, size_t>;
public:
    //using data_type = tuple_cat_t<this_data_type, typename tail::data_type>;
    //using remainder = typename tail::remainder;

    //static void serialize(data_type values, StreamSink* output) {
    //    serialize(&values.get<0>(output), &values.get<1>(output), output);
    //}

    static void serialize(StreamSink* output, const char * (&&str), size_t&& length, Ts&&... tail_values) {
        uint32_t i = str ? length : 0;
        size_t processed_bytes = 0;
        uint8_t buf[4];
        write_le<uint32_t>(i, buf, sizeof(buf));
        StreamSink::status_t status = output->process_bytes(buf, sizeof(buf), &processed_bytes);
        if (status != StreamSink::OK || processed_bytes != sizeof(buf)) {
            LOG_FIBRE_W(SERDES, "not everything processed");
            return;
        }

        if (str) {
            processed_bytes = 0;
            StreamSink::status_t status = output->process_bytes(reinterpret_cast<const uint8_t*>(str), length, &processed_bytes);
            if (processed_bytes != length) {
                LOG_FIBRE_W(SERDES, "not everything processed: ", processed_bytes);
            }
            if (status != StreamSink::OK) {
                LOG_FIBRE_W(SERDES, "error in output");
            }
        } else {
            LOG_FIBRE_W(SERDES, "attempt to serialize null string");
        }
        tail::serialize(output, std::forward<Ts>(tail_values)...);
    }
};



template<typename TStreamDecoders>
struct stream_decoder_chain_from_tuple;

template<typename... TStreamDecoders>
struct stream_decoder_chain_from_tuple<std::tuple<TStreamDecoders...>> {
    using type = StaticStreamChain<TStreamDecoders...>;
    using tuple_type = std::tuple<TStreamDecoders...>;
    using input_tuple_t = decltype(for_each_in_tuple(get_value_functor(), std::forward<const tuple_type>(std::declval<type>().get_all_streams())));
    static input_tuple_t get_inputs(const type& decoder) {
        return for_each_in_tuple(get_value_functor(), std::forward<const tuple_type>(decoder.get_all_streams()));
    }
};

template<typename TStreamDecoders>
struct encoder_chain_from_tuple;

template<typename... TEncoders>
struct encoder_chain_from_tuple<std::tuple<TEncoders...>> {
    using type = StaticStreamChain<TEncoders...>;
    using tuple_type = std::tuple<TEncoders...>;
    using input_tuple_t = decltype(for_each_in_tuple(get_value_functor(), std::forward<const tuple_type>(std::declval<type>().get_all_streams())));
    static input_tuple_t get_inputs(const type& decoder) {
        return for_each_in_tuple(get_value_functor(), std::forward<const tuple_type>(decoder.get_all_streams()));
    }
};


template<
    typename TFunc, TFunc& func,
    typename TMetadata, TMetadata& metadata>
class LocalFunctionEndpoint : public LocalEndpoint {
    using TDecoders = decoder_type<TMetadata::NInputs, args_of_t<TFunc>>;

    using out_arg_refs_t = typename TDecoders::remainder;
    using out_arg_vals_t = remove_refs_or_ptrs_from_tuple_t<out_arg_refs_t>;

    using TArgEncoders = encoder_type<TMetadata::NOutputs, out_arg_vals_t>;
    //using TRetEncoders = encoder_type<TMetadata::NOutputs, tuple_cat_t<out_arg_vals_t, as_tuple_t<result_of_t<TFunc>>>>;
    //int a =  TEncoders();
    //static_assert(
    //        (std::tuple_size<TEncoder::remainder> == 0) ||
    //        (std::tuple_size<TEncoder::remainder> == 1), "number of ");

    //using TDecoder = typename decoder_type<args_of_t<TFunc>>::type;
    //using TEncoder = typename decoder_type<args_of_t<TFunc>>::type;
    //static_assert(
    //    std::tuple_size<args_of_t<TFunc>> == TMetadata::NInputs,
    //    std::tuple_size<args_of_t<TFunc>> == TMetadata::NOu,
    //)
    //constexpr std::tuple_size<args_of_t<TFunc>> == NInputs

    using stream_chain = typename stream_decoder_chain_from_tuple<typename TDecoders::type>::type;
    //using encoder_chain = typename encoder_chain_from_tuple<typename TEncoders::type>::type;
    void open_connection(IncomingConnectionDecoder& incoming_connection_decoder) final {
        incoming_connection_decoder.set_stream<stream_chain>();
    }
    void decoder_finished(const IncomingConnectionDecoder& incoming_connection_decoder, OutputPipe* output) final {
        const stream_chain* decoder = incoming_connection_decoder.get_stream<stream_chain>();
        LOG_FIBRE(INPUT, "received all function arguments");
        //printf("arg decoder finished");
        //using tuple_type = std::tuple<FixedIntDecoder<TInputs, false>...>;
        //std::tuple<const TInputs&...> inputs = for_each_in_tuple(get_value_functor(), std::forward<const tuple_type>(decoder->get_all_streams()));
        auto in_refs = stream_decoder_chain_from_tuple<typename TDecoders::type>::get_inputs(*decoder);
        out_arg_vals_t out_vals;
        out_arg_refs_t out_refs =
            add_ref_or_ptr_to_tuple<out_arg_refs_t>::convert(std::forward<out_arg_vals_t>(out_vals));
        //auto outputs = eam_detfrom_tuple<typename TEncoders::type>::get_outputs();
        //std::tuple<TOutputs...> outputs;
        //std::tuple<TOutputs&...> output_refs = std::forward_as_tuple(outputs);
        //std::tuple<TOutputs&...> output_refs(std::get<0>(outputs));
        auto in_and_out_refs = std::tuple_cat(in_refs, out_refs);
        std::apply(func, in_and_out_refs);
        TArgEncoders::serialize(output, std::forward<out_arg_vals_t>(out_vals));
        //printf("first arg is ", std::hex, std::get<0>(inputs));
        //LOG_FIBRE(SERDES, "will write output of type ", typeid(TOutputs).name()...);
        //Serializer<std::tuple<TOutputs...>>::serialize(outputs, output);
    }

    uint16_t get_hash() {
        // TODO: implement
        return 0;
    }
    // @brief Returns a JSON snippet that describes this function
    bool get_as_json(const char ** output, size_t* length) final {
        return FunctionJSONAssembler<TMetadata, metadata>::get_as_json(output, length);
    }
};

}

#endif // __FIBRE_LOCAL_ENDPOINT
