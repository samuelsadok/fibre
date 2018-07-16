/**
 * @brief Configures Fibre
 */

/** @brief enable debugging */
#define DEBUG_FIBRE

/** @brief Allow Fibre to use C++ built-in threading facilities (std::thread, thread_local storage specifier) */
#define CONFIG_USE_STL_THREADING

/** @brief Allow Fibre to use C++ built-in std::chrono features */
#define CONFIG_USE_STL_CLOCK

/**
 * @brief Specifies how the output data is scheduled
 */
#define CONFIG_SCHEDULER_MODE   SCHEDULER_MODE_GLOBAL_THREAD


// This value must not be larger than USB_TX_DATA_SIZE defined in usbd_cdc_if.h
constexpr size_t TX_BUF_SIZE = 512; // does not work with 64 for some reason TODO: is this still valid?

/*
* One RX buffer per pipe is created.
* The RX buffer should be large enough to accomodate the function
* with the largest total immediate argument size.
*/
// larger values than 128 have currently no effect because of protocol limitations
// TODO: is this still the case?
constexpr size_t RX_BUF_SIZE = 512;
