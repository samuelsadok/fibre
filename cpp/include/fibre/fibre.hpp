#ifndef __FIBRE_HPP
#define __FIBRE_HPP

// Note that this option cannot be used to debug UART because it prints on UART
//#define DEBUG_FIBRE
#ifdef DEBUG_FIBRE
#define LOG_FIBRE(...)  do { printf(__VA_ARGS__); } while (0)
#else
#define LOG_FIBRE(...)  ((void) 0)
#endif

#include "protocol.hpp"

#endif // __FIBRE_HPP
