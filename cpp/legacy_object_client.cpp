
#include "crc.hpp"
#include "json.hpp"
#include "legacy_object_client.hpp"
#include "legacy_protocol.hpp"
#include "print_utils.hpp"
#include <fibre/simple_serdes.hpp>

using namespace fibre;

struct fibre::Transcoder {
    std::string app_codec;
    bool (*func)(LegacyObjectClient* client, std::vector<uint8_t>&);
    bool transcode(LegacyObjectClient* client, std::vector<uint8_t>& buf) {
        return (*func)(client, buf);
    }
};

Transcoder endpoint_ref_encoder{
    "object_ref", [](LegacyObjectClient* client, std::vector<uint8_t>& buf) {
        if (buf.size() < sizeof(uintptr_t)) {
            return false;
        }

        uintptr_t val = *reinterpret_cast<const uintptr_t*>(buf.data());
        LegacyObject* obj_ptr = reinterpret_cast<LegacyObject*>(val);
        uint8_t dst[4];
        write_le<uint16_t>(obj_ptr ? obj_ptr->ep_num : 0, dst);
        write_le<uint16_t>(obj_ptr ? obj_ptr->json_crc : 0, dst + 2);
        buf = std::vector<uint8_t>{dst, dst + 4};
        return true;
    }};
Transcoder endpoint_ref_decoder{
    "object_ref", [](LegacyObjectClient* client, std::vector<uint8_t>& buf) {
        if (buf.size() < 4) {
            return false;
        }

        uint16_t ep_num = read_le<uint16_t>(buf.data());
        uint16_t json_crc = read_le<uint16_t>(buf.data() + 2);

        LegacyObject* obj_ptr = nullptr;

        for (auto& known_obj : client->objects_) {
            if (known_obj->ep_num == ep_num &&
                known_obj->json_crc == json_crc) {
                obj_ptr = known_obj.get();
            }
        }

        buf =
            std::vector<uint8_t>{(uint8_t*)&obj_ptr, (uint8_t*)(&obj_ptr + 1)};
        return true;
    }};

std::unordered_map<std::string, Transcoder*> decoders = {
    {"endpoint_ref", &endpoint_ref_decoder}};
std::unordered_map<std::string, Transcoder*> encoders = {
    {"endpoint_ref", &endpoint_ref_encoder}};

std::vector<LegacyFibreArg> parse_arglist(
    const json_value& list_val,
    const std::unordered_map<std::string, Transcoder*>& transcoders,
    Logger logger) {
    std::vector<LegacyFibreArg> arglist;

    for (auto& arg :
         json_is_list(list_val) ? json_as_list(list_val) : json_list()) {
        if (!json_is_dict(*arg)) {
            F_LOG_E(logger, "arglist is invalid");
            continue;
        }
        auto dict = json_as_dict(*arg);

        json_value name_val = json_dict_find(dict, "name");
        json_value id_val = json_dict_find(dict, "id");
        json_value type_val = json_dict_find(dict, "type");

        if (!json_is_str(name_val) || !json_is_int(id_val) ||
            ((int)(size_t)json_as_int(id_val) != json_as_int(id_val)) ||
            !json_is_str(type_val)) {
            F_LOG_E(logger, "arglist is invalid");
            continue;
        }

        auto it = transcoders.find(json_as_str(type_val));

        arglist.push_back({json_as_str(name_val),
                           it == transcoders.end() ? json_as_str(type_val)
                                                   : it->second->app_codec,
                           it == transcoders.end() ? nullptr : it->second,
                           (size_t)json_as_int(id_val)});
    }

    return arglist;
}

void LegacyObjectClient::start(Node* node, Domain* domain,
                                EndpointClientCallback default_endpoint_client,
                                std::string path) {
    node_ = node;
    domain_ = domain;
    default_endpoint_client_ = default_endpoint_client;
    path_ = path;

    chunks_[0] = Chunk(0, data0);
    chunks_[1] = Chunk::frame_boundary(0);

    Socket* call = default_endpoint_client_.invoke(0, 1, {0}, {0}, this);

    tx_pos_ = BufChain{chunks_}.begin();
    while (tx_pos_.chunk != BufChain{chunks_}.c_end()) {
        WriteResult result = call->write(
            {{tx_pos_, BufChain{chunks_}.c_end()}, kFibreClosed});
        if (result.is_busy()) {
            return;
        }
        tx_pos_ = result.end;
    }
}

WriteResult LegacyObjectClient::write(WriteArgs args) {
    while (args.buf.n_chunks()) {
        auto front = args.buf.front();
        if (front.is_buf()) {
            for (uint8_t b : front.buf()) {
                json_.push_back(b);
            }
        } else {
            // end of JSON
        }
        args.buf = args.buf.skip_chunks(1);
    }
    if (args.status == kFibreClosed) {
        load_json(json_);
        json_ = {};  // free memory (but really who cares)
    }
    return {args.status, args.buf.begin()};
}

WriteArgs LegacyObjectClient::on_write_done(WriteResult result) {
    tx_pos_ = result.end;
    return {{tx_pos_, BufChain{chunks_}.c_end()}, kFibreClosed};
}

std::shared_ptr<LegacyInterface> LegacyObjectClient::get_property_interfaces(
    std::string codec, bool write) {
    auto& dict = write ? rw_property_interfaces : ro_property_interfaces;

    auto it = dict.find(codec);
    if (it != dict.end()) {
        return it->second;
    }

    auto intf_ptr = std::make_shared<LegacyInterface>();
    dict[codec] = intf_ptr;
    LegacyInterface& intf = *intf_ptr;

    auto encoder_it = encoders.find(codec);
    auto decoder_it = decoders.find(codec);
    Transcoder* encoder =
        encoder_it == encoders.end() ? nullptr : encoder_it->second;
    Transcoder* decoder =
        decoder_it == decoders.end() ? nullptr : decoder_it->second;

    intf.name = std::string{} + "fibre.Property<" +
                (write ? "readwrite" : "readonly") + " " + codec + ">";
    intf.functions.push_back(
        std::shared_ptr<LegacyFunction>(new LegacyFunction{
            this,
            "read",
            0,
            nullptr,
            {},
            {{"value", encoder ? encoder->app_codec : codec, encoder}}}));
    if (write) {
        intf.functions.push_back(
            std::shared_ptr<LegacyFunction>(new LegacyFunction{
                this,
                "exchange",
                0x4000,
                nullptr,
                {{"newval", encoder ? encoder->app_codec : codec, encoder}},
                {{"oldval", decoder ? decoder->app_codec : codec, decoder}}}));
    }

    return intf_ptr;
}

std::shared_ptr<LegacyObject> LegacyObjectClient::load_object(
    json_value list_val) {
    if (!json_is_list(list_val)) {
        F_LOG_E(domain_->ctx->logger, "interface members must be a list");
        return nullptr;
    }

    LegacyObject obj{
        .node = node_,
        .ep_num = 0,
        .json_crc = json_crc_,
        .intf = std::make_shared<LegacyInterface>(),
    };
    auto obj_ptr = std::make_shared<LegacyObject>(obj);
    LegacyInterface& intf = *obj_ptr->intf;

    for (auto& item : json_as_list(list_val)) {
        if (!json_is_dict(*item)) {
            F_LOG_E(domain_->ctx->logger, "expected dict");
            continue;
        }
        auto dict = json_as_dict(*item);

        json_value type = json_dict_find(dict, "type");
        json_value name_val = json_dict_find(dict, "name");
        std::string name =
            json_is_str(name_val) ? json_as_str(name_val) : "[anonymous]";

        if (json_is_str(type) && json_as_str(type) == "object") {
            std::shared_ptr<LegacyObject> subobj =
                load_object(json_dict_find(dict, "members"));
            intf.attributes.push_back({name, subobj});

        } else if (json_is_str(type) && json_as_str(type) == "function") {
            json_value id = json_dict_find(dict, "id");
            if (!json_is_int(id) ||
                ((int)(size_t)json_as_int(id) != json_as_int(id))) {
                continue;
            }
            intf.functions.push_back(
                std::shared_ptr<LegacyFunction>(new LegacyFunction{
                    this,
                    name,
                    (size_t)json_as_int(id),
                    obj_ptr.get(),
                    parse_arglist(json_dict_find(dict, "inputs"), encoders,
                                  domain_->ctx->logger),
                    parse_arglist(json_dict_find(dict, "outputs"), decoders,
                                  domain_->ctx->logger),
                }));

        } else if (json_is_str(type) && json_as_str(type) == "json") {
            // Ignore

        } else if (json_is_str(type)) {
            std::string type_str = json_as_str(type);
            json_value access = json_dict_find(dict, "access");
            std::string access_str =
                json_is_str(access) ? json_as_str(access) : "r";
            bool can_write = access_str.find('w') != std::string::npos;

            json_value id = json_dict_find(dict, "id");
            if (!json_is_int(id) ||
                ((int)(size_t)json_as_int(id) != json_as_int(id))) {
                continue;
            }

            LegacyObject subobj{
                .node = node_,
                .ep_num = (size_t)json_as_int(id),
                .json_crc = json_crc_,
                .intf = get_property_interfaces(type_str, can_write),
            };
            auto subobj_ptr = std::make_shared<LegacyObject>(subobj);
            objects_.push_back(subobj_ptr);
            intf.attributes.push_back({name, subobj_ptr});

        } else {
            F_LOG_E(domain_->ctx->logger, "unsupported codec");
        }
    }

    objects_.push_back(obj_ptr);
    return obj_ptr;
}

void LegacyObjectClient::load_json(cbufptr_t json) {
    F_LOG_D(domain_->ctx->logger, "received JSON of length " << json.size());

    const char* begin = reinterpret_cast<const char*>(json.begin());
    auto val = json_parse(&begin, begin + json.size(), domain_->ctx->logger);

    if (json_is_err(val)) {
        size_t pos =
            json_as_err(val).ptr - reinterpret_cast<const char*>(json.begin());
        F_LOG_E(domain_->ctx->logger,
                "JSON parsing error: " << json_as_err(val).str
                                       << " at position " << pos);
        return;
    } else if (!json_is_list(val)) {
        F_LOG_E(domain_->ctx->logger, "JSON data must be a list");
        return;
    }

    F_LOG_D(domain_->ctx->logger, "sucessfully parsed JSON");
    json_crc_ = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(
        PROTOCOL_VERSION, json_.data(), json_.size());
    root_obj_ = load_object(val);
    if (root_obj_) {
        domain_->on_found_root_object(
            reinterpret_cast<Object*>(root_obj_.get()), root_obj_->intf.get(),
            path_);
    }
}

FunctionInfo* LegacyFunction::get_info() const {
    FunctionInfo* info = new FunctionInfo{
        .name = name, .inputs = {{"obj", "object_ref"}}, .outputs = {}};

    for (auto& arg : inputs_) {
        info->inputs.push_back({arg.name, arg.app_codec});
    }
    for (auto& arg : outputs_) {
        info->outputs.push_back({arg.name, arg.app_codec});
    }

    return info;
}

void LegacyFunction::free_info(FunctionInfo* info) const {
    delete info;
}

InterfaceInfo* LegacyInterface::get_info() {
    InterfaceInfo* info = new InterfaceInfo{};

    info->name = name;

    for (auto& func : functions) {
        info->functions.push_back(func.get());
    }

    for (auto& attr : attributes) {
        info->attributes.push_back(
            {.name = attr.name, .intf = attr.object->intf.get()});
    }

    return info;
}

void LegacyInterface::free_info(InterfaceInfo* info) {
    delete info;
}

RichStatusOr<Object*> LegacyInterface::get_attribute(Object* parent_obj,
                                                      size_t attr_id) {
    fibre::LegacyObject* parent_obj_cast =
        reinterpret_cast<fibre::LegacyObject*>(parent_obj);
    F_RET_IF(parent_obj_cast->intf.get() != this,
             "object does not implement this interface");
    F_RET_IF(attr_id >= attributes.size(),
             "attribute ID " << attr_id << " out of range, have only "
                             << attributes.size() << " attributes");
    return reinterpret_cast<Object*>(attributes[attr_id].object.get());
}

struct LegacyCallContext2;

struct TheStateMachine {
    TheStateMachine(LegacyCallContext2* ctx, int start_arg,
                    const std::vector<fibre::LegacyFibreArg>& args)
        : ctx_(ctx), start_arg(start_arg), args_(args) {}
    Cont iteration(WriteArgs args);
    Cont iteration(WriteResult result);
    Cont extended_iteration(WriteArgs args);
    Cont extended_iteration(WriteResult result);
    WriteResult write(WriteArgs args, Socket** sink);
    WriteArgs on_write_done(WriteResult result, Socket** source);

    LegacyCallContext2* ctx_;
    size_t start_arg;
    const std::vector<fibre::LegacyFibreArg>& args_;
    size_t arg_num_ = 0;
    std::vector<uint8_t> buf;
    Chunk chunks_[2];
    Status status_ = kFibreClosed;
    bool terminated_ = false;
    bool changed_state = false;
    WriteArgs pending;
};

struct LegacyCallContext2 final : TwoSidedSocket {
    LegacyCallContext2(const LegacyFunction* func, Socket* caller);

    const LegacyFunction* func_;

    Socket* caller_;
    Socket* callee_;

    TheStateMachine tx_state_machine_;
    TheStateMachine rx_state_machine_;

    bool upstream_closed_ = false;
    bool downstream_closed_ = false;

    WriteResult downstream_write(WriteArgs args) final;
    WriteArgs on_upstream_write_done(WriteResult result) final;
    WriteResult upstream_write(WriteArgs args) final;
    WriteArgs on_downstream_write_done(WriteResult result) final;

    void maybe_close(WriteResult result, bool& closed);
};

std::vector<uint16_t> get_arg_eps(const std::vector<LegacyFibreArg>& args,
                                  uint16_t ep_offset) {
    std::vector<uint16_t> eps;
    std::transform(
        args.begin(), args.end(), std::inserter(eps, eps.end()),
        [&](const LegacyFibreArg& arg) { return arg.ep_num + ep_offset; });
    return eps;
}

Cont TheStateMachine::iteration(WriteArgs args) {
    if (terminated_) {
        // Closed - drop all input
        if (args.status == kFibreOk || args.buf.n_chunks()) {
            F_LOG_W(ctx_->func_->client->domain_->ctx->logger, "received excess data");
            args.status = kFibreClosed;
        }
        return Cont1{args.status, args.buf.end()};
    }

    if (arg_num_ < start_arg) {
        // Receiving first argument (object pointer)

        while (args.buf.n_chunks()) {
            Chunk chunk = args.buf.front();
            args.buf = args.buf.skip_chunks(1);

            if (chunk.is_buf() && chunk.layer() == 0) {
                for (auto b : chunk.buf()) {
                    buf.push_back(b);
                }
            } else if (chunk.is_frame_boundary() && chunk.layer() == 0) {
                if (buf.size() == sizeof(void*)) {
                    LegacyObject* obj = *(LegacyObject**)&buf[0];
                    uint16_t endpoint_id = ctx_->func_->ep_num + obj->ep_num;
                    ctx_->callee_ =
                        ctx_->func_->client->default_endpoint_client_.invoke(
                            endpoint_id, obj->json_crc,
                            get_arg_eps(ctx_->func_->inputs_, obj->ep_num),
                            get_arg_eps(ctx_->func_->outputs_, obj->ep_num),
                            ctx_->downfacing_socket());
                    arg_num_++;
                    changed_state = true;
                    buf = {};
                    return Cont1{kFibreOk, args.buf.begin()};
                } else {
                    arg_num_ = args_.size() + start_arg;
                    status_ = kFibreInternalError;
                    changed_state = true;
                    return Cont1{kFibreInternalError, args.buf.begin()};
                }

            } else {
                arg_num_ = args_.size() + start_arg;
                status_ = kFibreInternalError;
                changed_state = true;
                return Cont1{kFibreInternalError, args.buf.begin()};
            }
        }
        return Cont1{kFibreOk, args.buf.begin()};

    } else if (arg_num_ < start_arg + args_.size() &&
               args_[arg_num_ - start_arg].transcoder) {
        Transcoder* transcoder = args_[arg_num_ - start_arg].transcoder;
        // Receiving transcoded argument
        while (args.buf.n_chunks()) {
            Chunk chunk = args.buf.front();
            args.buf = args.buf.skip_chunks(1);

            if (chunk.is_buf() && chunk.layer() == 0) {
                for (auto b : chunk.buf()) {
                    buf.push_back(b);
                }
            } else if (chunk.is_frame_boundary() && chunk.layer() == 0) {
                if (!transcoder->transcode(ctx_->func_->client, buf)) {
                    arg_num_ = args_.size() + start_arg;
                    status_ = kFibreInternalError;
                    changed_state = true;
                    return Cont1{kFibreInternalError, args.buf.begin()};
                }

                // TODO: buf should probably be reset somewhere, otherwise we
                // can't transcode two arguments in a row.

                chunks_[0] = Chunk{0, buf};
                chunks_[1] = Chunk::frame_boundary(0);
                changed_state = true;
                pending = args;
                return Cont0{chunks_, kFibreOk};
            } else {
                arg_num_ = args_.size() + start_arg;
                status_ = kFibreInternalError;
                changed_state = true;
                return Cont1{kFibreInternalError, args.buf.begin()};
            }
        }
        return Cont1{kFibreOk, args.buf.begin()};

    } else if (arg_num_ < start_arg + args_.size()) {
        // Receiving non-transcoded argument
        if (!args.buf.n_chunks() && args.status == kFibreOk) {
            return Cont1{args.status, args.buf.begin()};
        }
        CBufIt arg_end = args.buf.find_layer0_bound();

        if (arg_end == args.buf.end()) {
            // don't move iterator ahead
        } else if (arg_end.chunk + 1 == args.buf.end().chunk) {
            arg_end = args.buf.end();
        } else {
            arg_end = CBufIt{arg_end.chunk + 1, (arg_end.chunk + 1)->buf().begin()};
        }
        pending = args;
        return Cont0{args.buf.until(arg_end.chunk), kFibreOk};

    } else {
        // Closing
        pending = args;
        return Cont0{{}, status_};
    }
}

Cont TheStateMachine::iteration(WriteResult result) {
    if (result.status != kFibreOk) {
        terminated_ = true;
        return Cont1{result.status, pending.buf.begin()};

    } else if (arg_num_ < start_arg) {
        // First argument (object pointer)
        return Cont0{{}, kFibreInternalError};  // shouldn't happen

    } else if (arg_num_ < start_arg + args_.size() &&
               args_[arg_num_ - start_arg].transcoder) {
        // Transcoded argument
        if (result.end.chunk == chunks_ + 2) {
            arg_num_++;
            changed_state = true;
            return extended_iteration(pending);
        } else {
            return Cont0{BufChain{result.end, chunks_ + 2}, kFibreOk};
        }

    } else if (arg_num_ < start_arg + args_.size()) {
        // Non-transcoded argument
        BufChain sent = pending.buf.until(result.end.chunk);
        if (sent.n_chunks() &&
            sent.back().is_frame_boundary() &&
            sent.back().layer() == 0) {
            arg_num_++;
        }
        pending.buf = pending.buf.from(result.end);
        return extended_iteration(pending);

    } else if (!terminated_) {
        // Closing
        terminated_ = true;
        return extended_iteration({pending.buf, result.status});

    } else {
        // Closed
        return Cont0{{}, result.status};  // shouldn't happen
    }
}

Cont TheStateMachine::extended_iteration(WriteArgs args) {
    do {
        changed_state = false;

        auto output = iteration(args);

        if (output.index() == 1) {
            // iteration ate some of the buffer and gave a new state
            args.buf = args.buf.from(std::get<1>(output).end);
        } else if (output.index() == 0) {
            // iteration returned a buffer
            return Cont0{std::get<0>(output)};
        }
    } while (args.buf.n_chunks() || changed_state);
    return Cont1{args.status, args.buf.begin()};
}

Cont TheStateMachine::extended_iteration(WriteResult result) {
    return iteration(result);
}

WriteResult TheStateMachine::write(WriteArgs args, Socket** sink) {
    Cont cont = extended_iteration(args);
    for (;;) {
        if (cont.index() == 1) {
            return std::get<1>(cont);
        }
        WriteResult result = (*sink)->write(std::get<0>(cont));
        if (result.is_busy()) {
            return WriteResult::busy();
        }
        cont = extended_iteration(result);
    }
}

WriteArgs TheStateMachine::on_write_done(WriteResult result, Socket** source) {
    Cont cont = extended_iteration(result);
    for (;;) {
        if (cont.index() == 0) {
            return std::get<0>(cont);
        }
        WriteArgs args = (*source)->on_write_done(std::get<1>(cont));
        if (std::get<1>(cont).status != kFibreOk) {
            return {{}, std::get<1>(cont).status};
        }
        if (args.is_busy()) {
            return WriteArgs::busy();
        }
        cont = extended_iteration(args);
    }
}

LegacyCallContext2::LegacyCallContext2(const LegacyFunction* func, Socket* caller)
    : func_(func),
      caller_(caller),
      tx_state_machine_{this, 1, func->inputs_},
      rx_state_machine_{this, 0, func->outputs_} {}

Socket* LegacyFunction::start_call(Domain* domain, bufptr_t call_frame,
                                    Socket* caller) const {
    // Instantiate new call (TODO: free)
    return alloc_ctx<LegacyCallContext2>(call_frame, this, caller)
        ->upfacing_socket();
}

WriteResult LegacyCallContext2::downstream_write(WriteArgs args) {
    auto result = tx_state_machine_.write(args, &callee_);
    maybe_close(result, downstream_closed_);
    return result;
}

WriteArgs LegacyCallContext2::on_downstream_write_done(WriteResult result) {
    auto args = tx_state_machine_.on_write_done(result, &caller_);
    maybe_close(result, downstream_closed_);
    return args;
}

WriteResult LegacyCallContext2::upstream_write(WriteArgs args) {
    auto result = rx_state_machine_.write(args, &caller_);
    maybe_close(result, upstream_closed_);
    return result;
}

WriteArgs LegacyCallContext2::on_upstream_write_done(WriteResult result) {
    auto args = rx_state_machine_.on_write_done(result, &callee_);
    maybe_close(result, upstream_closed_);
    return args;
}

void LegacyCallContext2::maybe_close(WriteResult result, bool& closed) {
    if (result.status != Status::kFibreOk && result.status != Status::kFibreBusy) {
        closed = true;
    }
    if (upstream_closed_ && downstream_closed_) {
        delete this;
    }
}
