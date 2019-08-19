#ifndef __FIBRE_CALLBACK_HPP
#define __FIBRE_CALLBACK_HPP

#include <tuple>

namespace fibre {

template<typename ... T>
struct Callback {
    void (*callback)(void*, T...);
    void* ctx;
};

/*template<typename TCaptures, typename TArgs>
struct CallbackWithCtx;

template<typename ... TCapture, typename ... TArg>
struct CallbackWithCtx<std::tuple<TCapture...>, std::tuple<TArg...>> {
    void (*callback)(TCapture... , TArg...);
    std::tuple<TCapture...> ctx;
};*/

}
#endif // __FIBRE_CALLBACK_HPP