
// This file must be provided by the application. Make sure its containing
// directory is in the compiler's include search path.
// The file can be empty to accept the default configuration.
#include <fibre_config.hpp>


// Default configuration (keep consistent with README!)

#ifndef FIBRE_ENABLE_SERVER
#define FIBRE_ENABLE_SERVER 0
#endif

#ifndef FIBRE_ENABLE_CLIENT
#define FIBRE_ENABLE_CLIENT 0
#endif

#ifndef FIBRE_ENABLE_EVENT_LOOP
#define FIBRE_ENABLE_EVENT_LOOP 0
#endif

#ifndef FIBRE_ALLOW_HEAP
#define FIBRE_ALLOW_HEAP 1
#endif

#ifndef FIBRE_MAX_LOG_VERBOSITY
#define FIBRE_MAX_LOG_VERBOSITY 5
#endif

#ifndef FIBRE_ENABLE_TEXT_LOGGING
#define FIBRE_ENABLE_TEXT_LOGGING 1
#endif

#ifndef FIBRE_ENABLE_CAN_ADAPTER
#define FIBRE_ENABLE_CAN_ADAPTER 0
#endif

#ifndef FIBRE_ENABLE_LIBUSB_BACKEND
#define FIBRE_ENABLE_LIBUSB_BACKEND 0
#endif

#ifndef FIBRE_ENABLE_TCP_CLIENT_BACKEND
#define FIBRE_ENABLE_TCP_CLIENT_BACKEND 0
#endif

#ifndef FIBRE_ENABLE_TCP_SERVER_BACKEND
#define FIBRE_ENABLE_TCP_SERVER_BACKEND 0
#endif

#ifndef FIBRE_ENABLE_SOCKET_CAN_BACKEND
#define FIBRE_ENABLE_SOCKET_CAN_BACKEND 0
#endif

#define F_RUNTIME_CONFIG 2

#if FIBRE_ENABLE_CLIENT == 0
#define F_CONFIG_ENABLE_SERVER_T std::integral_constant<bool, false>
#define F_CONFIG_ENABLE_CLIENT_T std::integral_constant<bool, false>
#elif FIBRE_ENABLE_CLIENT == 1
#define F_CONFIG_ENABLE_SERVER_T std::integral_constant<bool, true>
#define F_CONFIG_ENABLE_CLIENT_T std::integral_constant<bool, true>
#else
#define F_CONFIG_ENABLE_SERVER_T bool
#define F_CONFIG_ENABLE_CLIENT_T bool
#endif
