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
    virtual void open_connection(IncomingConnectionDecoder& input) const = 0;
    virtual void decoder_finished(const IncomingConnectionDecoder& input, OutputPipe* output) const = 0;
    virtual uint16_t get_hash() const = 0;
    virtual bool get_as_json(const char ** output, size_t* length) const = 0;
};


/**
 * @brief Assembles a JSON snippet that describes a function
 */
template<typename TMetadata>
struct FunctionJSONAssembler {
private:
    template<size_t I>
    using get_input_json_t = static_string<11 + std::tuple_element_t<1, std::tuple_element_t<I, typename TMetadata::TInputMetadata>>::size()>;

    template<size_t I>
    static constexpr get_input_json_t<I>
    get_input_json(const TMetadata& metadata) {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            std::get<1>(std::get<I>(metadata.get_input_metadata())),
            make_const_string("\"}")
        );
    }

    template<typename Is>
    struct get_all_inputs_json_type;
    template<size_t... Is>
    struct get_all_inputs_json_type<std::index_sequence<Is...>> {
        using type = const_str_join_t<1, get_input_json_t<Is>::size()...>;
    };
    using get_all_inputs_json_t = typename get_all_inputs_json_type<std::make_index_sequence<TMetadata::NInputs>>::type;

    template<size_t... Is>
    static constexpr get_all_inputs_json_t
    get_all_inputs_json(const TMetadata& metadata, std::index_sequence<Is...>) {
        return const_str_join(make_const_string(","), get_input_json<Is>(metadata)...);
    }

public:
    using get_json_t = static_string<19 + TMetadata::TFuncName::size() + get_all_inputs_json_t::size()>;

    // @brief Returns a JSON snippet that describes this function
    static constexpr get_json_t
    get_as_json(const TMetadata& metadata) {
        return const_str_concat(
            make_const_string("{\"name\":\""),
            metadata.get_function_name(),
            make_const_string("\",\"in\":["),
            get_all_inputs_json(metadata, std::make_index_sequence<TMetadata::NInputs>()),
            make_const_string("]}")
        );
    }
};

template<typename T>
struct nothing {};

struct input_metadata_item_tag {};
struct output_metadata_item_tag {};

template<size_t INameLength, typename TIn>
using InputMetadata = std::tuple<input_metadata_item_tag, static_string<INameLength>, nothing<TIn>>;

template<size_t INameLength, typename TOut>
using OutputMetadata = std::tuple<output_metadata_item_tag, static_string<INameLength>, nothing<TOut>>;

template<typename TIn, size_t INameLengthPlus1>
constexpr InputMetadata<(INameLengthPlus1-1), TIn> make_input_metadata(const char (&name)[INameLengthPlus1]) {
    return InputMetadata<(INameLengthPlus1-1), TIn>(
        input_metadata_item_tag(),
        make_const_string(name),
        nothing<TIn>());
}

template<typename TOut, size_t INameLengthPlus1>
constexpr OutputMetadata<(INameLengthPlus1-1), TOut> make_output_metadata(const char (&name)[INameLengthPlus1]) {
    return OutputMetadata<(INameLengthPlus1-1), TOut>(
        output_metadata_item_tag(),
        make_const_string(name),
        nothing<TOut>());
}


template<typename TFuncName, typename TInputMetadata, typename TOutputMetadata>
struct StaticFunctionMetadata;

template<size_t IFun, typename... TInputs, typename... TOutputs>
struct StaticFunctionMetadata<
        static_string<IFun>,
        std::tuple<TInputs...>,
        std::tuple<TOutputs...>> {
    static constexpr const size_t NInputs = (sizeof...(TInputs));
    static constexpr const size_t NOutputs = (sizeof...(TOutputs));
    using TFuncName = static_string<IFun>;
    using TInputMetadata = std::tuple<TInputs...>;
    using TOutputMetadata = std::tuple<TOutputs...>;

    TFuncName function_name;
    TInputMetadata input_metadata;
    TOutputMetadata output_metadata;

    constexpr StaticFunctionMetadata(TFuncName function_name, TInputMetadata input_metadata, TOutputMetadata output_metadata)
        : function_name(function_name), input_metadata(input_metadata), output_metadata(output_metadata) {}

    template<size_t INameLength, typename TIn>
    constexpr StaticFunctionMetadata<TFuncName, tuple_cat_t<TInputMetadata, std::tuple<InputMetadata<INameLength, TIn>>>, TOutputMetadata> with_item(InputMetadata<INameLength, TIn> item) {
        return StaticFunctionMetadata<TFuncName, tuple_cat_t<TInputMetadata, std::tuple<InputMetadata<INameLength, TIn>>>, TOutputMetadata>(
            function_name,
            std::tuple_cat(input_metadata, std::make_tuple(item)),
            output_metadata);
    }

    template<size_t INameLength, typename TOut>
    constexpr StaticFunctionMetadata<TFuncName, TInputMetadata, tuple_cat_t<TOutputMetadata, std::tuple<OutputMetadata<INameLength, TOut>>>> with_item(OutputMetadata<INameLength, TOut> item) {
        return StaticFunctionMetadata<TFuncName, TInputMetadata, tuple_cat_t<TOutputMetadata, std::tuple<OutputMetadata<INameLength, TOut>>>>(
            function_name,
            input_metadata,
            std::tuple_cat(output_metadata, std::make_tuple(item)));
    }

    constexpr StaticFunctionMetadata with_items() {
        return *this;
    }

    template<typename T, typename ... Ts>
    constexpr auto with_items(T item, Ts... items) 
            -> decltype(with_item(item).with_items(items...)) {
        return with_item(item).with_items(items...);
    }

    constexpr TFuncName get_function_name() { return function_name; }
    constexpr TInputMetadata get_input_metadata() { return input_metadata; }
    constexpr TOutputMetadata get_output_metadata() { return output_metadata; }

    using JSONAssembler = FunctionJSONAssembler<StaticFunctionMetadata>;
    typename JSONAssembler::get_json_t json = JSONAssembler::get_as_json(*this);
};


template<size_t INameLength_Plus1>
static constexpr StaticFunctionMetadata<static_string<INameLength_Plus1-1>, std::tuple<>, std::tuple<>> make_function_props(const char (&function_name)[INameLength_Plus1]) {
    return StaticFunctionMetadata<static_string<INameLength_Plus1-1>, std::tuple<>, std::tuple<>>(static_string<INameLength_Plus1-1>(function_name), std::tuple<>(), std::tuple<>());
}

struct get_value_functor {
    template<typename T>
    decltype(std::declval<T>().get_value()) operator () (T&& t) {
        return t.get_value();
    }
};



template<typename, size_t NInputs, typename... Ts>
struct decoder_type;

template<size_t NInputs>
struct decoder_type<void, NInputs> {
    static_assert(NInputs == 0, "too many input names provided");
    using type = std::tuple<>;
    using remainder = std::tuple<>;
};

template<typename T, typename... Ts>
struct decoder_type<void, 0, T, Ts...> {
    using type = std::tuple<>;
    using remainder = std::tuple<T, Ts...>;
};

template<size_t NInputs, typename... TTuple, typename... Ts>
struct decoder_type<typename std::enable_if_t<(NInputs > 0)>, NInputs, std::tuple<TTuple...>, Ts...> {
private:
    using unpacked = decoder_type<void, NInputs, TTuple..., Ts...>;
public:
    using type = typename unpacked::type;
    using remainder = typename unpacked::remainder;
};

template<size_t NInputs, typename... Ts>
struct decoder_type<typename std::enable_if_t<(NInputs > 0)>, NInputs, uint32_t, Ts...> {
private:
    using tail = decoder_type<void, NInputs - 1, Ts...>;
    using this_type = FixedIntDecoder<uint32_t, false>;
public:
    using type = tuple_cat_t<std::tuple<this_type>, typename tail::type>;
    using remainder = typename tail::remainder;
};

template<size_t NInputs, typename TObj, typename... Ts>
struct decoder_type<typename std::enable_if<(NInputs > 0)>::type, NInputs, TObj*, Ts...> {
private:
    using tail = decoder_type<void, NInputs - 1, Ts...>;
    using this_type = ObjectReferenceDecoder<TObj>;
public:
    using type = tuple_cat_t<std::tuple<this_type>, typename tail::type>;
    using remainder = typename tail::remainder;
};

template<typename, size_t NOutputs, typename... Ts>
struct encoder_type;

template<size_t NOutputs>
struct encoder_type<void, NOutputs> {
    static_assert(NOutputs == 0, "too many output names provided");
    using type = std::tuple<>;
    using remainder = std::tuple<>;
    static void serialize(StreamSink* output) {
        // nothing to do
    }
};

template<typename T, typename... Ts>
struct encoder_type<void, 0, T, Ts...> {
    using type = std::tuple<>;
    using remainder = std::tuple<T, Ts...>;
    static void serialize(StreamSink* output, T&& value, Ts&&... tail_values) {
        // nothing to do
    }
};

template<size_t NOutputs, typename... TTuple, typename... Ts>
struct encoder_type<typename std::enable_if_t<(NOutputs > 0)>, NOutputs, std::tuple<TTuple...>, Ts...> {
private:
    using unpacked = encoder_type<void, NOutputs, TTuple..., Ts...>;
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
struct encoder_type<typename std::enable_if_t<(NOutputs > 0)>, NOutputs, uint32_t, Ts...> {
private:
    using tail = encoder_type<void, NOutputs - 1, Ts...>;
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
struct encoder_type<typename std::enable_if_t<(NOutputs > 0)>, NOutputs, const char *, size_t, Ts...> {
private:
    using tail = encoder_type<void, NOutputs - 1, Ts...>;
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
    typename TFunc,
    //typename TFuncSignature,
    typename TMetadata>
class LocalFunctionEndpoint : public LocalEndpoint {
    using TDecoders = decoder_type<void, TMetadata::NInputs, args_of_t<TFunc>>;

    using out_arg_refs_t = typename TDecoders::remainder;
    using out_arg_vals_t = remove_refs_or_ptrs_from_tuple_t<out_arg_refs_t>;

    using TArgEncoders = encoder_type<void, TMetadata::NOutputs, out_arg_vals_t>;
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

public:
    //constexpr LocalFunctionEndpoint(TMetadata metadata) : metadata_(metadata), json_(json) {}
    constexpr LocalFunctionEndpoint(TFunc&& func, TMetadata&& metadata) :
        func_(std::forward<TFunc>(func)),
        metadata_(std::forward<TMetadata>(metadata)) {}

    using stream_chain = typename stream_decoder_chain_from_tuple<typename TDecoders::type>::type;
    //using encoder_chain = typename encoder_chain_from_tuple<typename TEncoders::type>::type;
    void open_connection(IncomingConnectionDecoder& incoming_connection_decoder) const final {
        incoming_connection_decoder.set_stream<stream_chain>();
    }
    void decoder_finished(const IncomingConnectionDecoder& incoming_connection_decoder, OutputPipe* output) const final {
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
        std::apply(func_, in_and_out_refs);
        TArgEncoders::serialize(output, std::forward<out_arg_vals_t>(out_vals));
        //printf("first arg is ", std::hex, std::get<0>(inputs));
        //LOG_FIBRE(SERDES, "will write output of type ", typeid(TOutputs).name()...);
        //Serializer<std::tuple<TOutputs...>>::serialize(outputs, output);
    }

    uint16_t get_hash() const final {
        // TODO: implement
        return 0;
    }
    // @brief Returns a JSON snippet that describes this function
    bool get_as_json(const char ** output, size_t* length) const final {
        if (output) *output = metadata_.json.c_str();
        if (length) *length = metadata_.json.size();
        return true;
    }

private:
    TFunc func_;
    TMetadata metadata_;
};

/*template<typename TFunc, TFunc& func, typename TMetadata>
LocalFunctionEndpoint<TFunc, TMetadata> make_local_function_endpoint0(const TMetadata metadata) {
    static const auto json = FunctionJSONAssembler<TMetadata>::get_as_json(metadata);
    using ret_t = LocalFunctionEndpoint<TFunc, TMetadata>;
    return ret_t(func, json.c_str(), decltype(json)::size());
};*/

template<typename TFunc, typename TMetadata>
LocalFunctionEndpoint<TFunc&, TMetadata> make_local_function_endpoint(TFunc& func, TMetadata&& metadata) {
    using ret_t = LocalFunctionEndpoint<TFunc&, TMetadata>;
    return ret_t(std::forward<TFunc>(func), std::forward<TMetadata>(metadata));
};


template<typename TFunc, typename TMetadata>
LocalFunctionEndpoint<TFunc, TMetadata> make_local_function_endpoint(TFunc&& func, TMetadata&& metadata) {
    using ret_t = LocalFunctionEndpoint<TFunc, TMetadata>;
    return ret_t(std::forward<TFunc>(func), std::forward<TMetadata>(metadata));
};

}

#endif // __FIBRE_LOCAL_ENDPOINT
