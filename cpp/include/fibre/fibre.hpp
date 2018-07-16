#ifndef __FIBRE_HPP
#define __FIBRE_HPP

#include <stdint.h>
#include <stdlib.h>

// TODO: remove
#include <vector>
#include <unordered_map>

/**
 * @brief Don't launch any scheduler thread.
 * 
 * The user application must call fibre::schedule_all() periodically, otherwise
 * Fibre will not emit any data. This option is intended for systems that don't
 * support threading.
 */
#define SCHEDULER_MODE_MANUAL 1

/**
 * @brief Launch one global scheduler thread that will handle all remote nodes.
 * This is recommended for embedded systems that don't support dynamic memory.
 */
#define SCHEDULER_MODE_GLOBAL_THREAD 2

/**
 * @brief Launch one scheduler thread per remote node.
 * This is recommended for desktop class systems.
 */
#define SCHEDULER_MODE_PER_NODE_THREAD 3



// log topics
//struct CONFIG_LOG_INPUT {};
//struct CONFIG_LOG_OUTPUT {};
//struct CONFIG_LOG_GENERAL {};
//struct CONFIG_LOG_TCP {};
//struct CONFIG_LOG_USB {};

#define LOG_TOPICS(X) \
    X(GENERAL, "general") \
    X(INPUT, "input") \
    X(OUTPUT, "output") \
    X(SERDES, "serdes") \
    X(TCP, "tcp")

#include "logging.hpp"

#include "fibre_config.hpp"

//#ifdef DEBUG_FIBRE
//#define LOG_FIBRE(...)  do { printf("%s %s(): ", __FILE__, __func__); printf(__VA_ARGS__); printf("\r\n"); } while (0)
//#else
//#define LOG_FIBRE(...)  ((void) 0)
//#endif



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


/* Forward declarations ------------------------------------------------------*/

#include "uuid.hpp"
#include "threading_utils.hpp"

namespace fibre {
    struct global_state_t;

    class IncomingConnectionDecoder; // input.hpp
    class OutputPipe; // output.hpp
    class RemoteNode; // remote_node.hpp
    class LocalEndpoint; // local_function.hpp
}

#include "stream.hpp"
#include "decoders.hpp"
#include "input.hpp"
#include "output.hpp"
#include "remote_node.hpp"
#include "local_function.hpp"
#include "types.hpp"

namespace fibre {
    struct global_state_t {
        bool initialized = false;
        Uuid own_uuid;
        std::unordered_map<Uuid, RemoteNode> remote_nodes_;
        std::vector<fibre::FibreRefType*> ref_types_ = std::vector<fibre::FibreRefType*>();
        std::vector<fibre::LocalEndpoint*> functions_ = std::vector<fibre::LocalEndpoint*>();
        std::thread scheduler_thread;
        AutoResetEvent output_pipe_ready;
        AutoResetEvent output_channel_ready;
    };

    extern global_state_t global_state;

    void init();
    void publish_function(LocalEndpoint* function);
/*
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
        publish_ref_type(&asd);
        // TODO: publish object
        return 0;
    }
    void publish_ref_type(FibreRefType* type);
    */

    RemoteNode* get_remote_node(Uuid uuid);

#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_MANUAL
    void schedule_all();
#endif
} // namespace fibre

#endif // __FIBRE_HPP
