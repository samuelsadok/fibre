#ifndef __FIBRE_CALLBACK_LIST_HPP
#define __FIBRE_CALLBACK_LIST_HPP

#include <tuple>
#include <algorithm>
#include "cpp_utils.hpp"

namespace fibre {

template<typename ... TArgs>
class CallbackList {
public:
    CallbackList() {}

    // Delete the copy constructor because copying/moving the list would mean
    // that when we trigger the original instance, this will not have any effect
    // on the clone.
    // TODO: this is lame. That way we can't copy any object that contains this.
    // maybe we should delete the copy constructor and keep the move constructor.
    // Or make the callback list behave like a handle (with a central object registry)
    //CallbackList(const CallbackList & other) = delete;

    CallbackList& operator+=(Callback<TArgs...>* callback) {
        callbacks_.push_back(callback);
        return *this;
    }

    // TODO: these overloads are bad because they have difficulties returning
    // errors (without using Exceptions)
    CallbackList& operator-=(Callback<TArgs...>* callback) {
        auto it = std::find(callbacks_.begin(), callbacks_.end(), callback);
        if (it != callbacks_.end()) {
            callbacks_.erase(it);
        } else {
            throw;
            //FIBRE_LOG(E) << "attempt to deregister a callback more than once";
        }
        return *this;
    }

    void trigger(TArgs... args) const {
        for (auto it: callbacks_) {
            if (it) {
                (*it)(args...);
            }
        }
    }

    size_t size() const { return callbacks_.size(); }

private:
    // TODO: fix dynamic memory
    std::vector<Callback<TArgs...>*> callbacks_;
};

}
#endif // __FIBRE_CALLBACK_LIST_HPP