#ifndef __FIBRE_PROPERTY_HPP
#define __FIBRE_PROPERTY_HPP

namespace fibre {

template<typename T>
struct Property {
    Property(void* ctx, T(*getter)(void*), void(*setter)(void*, T))
        : ctx_(ctx), getter_(getter), setter_(setter) {}
    Property(T* ctx)
        : ctx_(ctx), getter_([](void* ctx){ return *(T*)ctx; }), setter_([](void* ctx, T val){ *(T*)ctx = val; }) {}
    Property& operator*() { return *this; }
    Property* operator->() { return this; }

    T read() const {
        return (*getter_)(ctx_);
    }

    T exchange(T value) const {
        T old_value = (*getter_)(ctx_);
        (*setter_)(ctx_, value);
        return old_value;
    }
    
    void* ctx_;
    T(*getter_)(void*);
    void(*setter_)(void*, T);
};

template<typename T>
struct Property<const T> {
    Property(void* ctx, T(*getter)(void*))
        : ctx_(ctx), getter_(getter) {}
    Property(const T* ctx)
        : ctx_(const_cast<T*>(ctx)), getter_([](void* ctx){ return *(const T*)ctx; }) {}
    Property& operator*() { return *this; }
    Property* operator->() { return this; }

    T read() const {
        return (*getter_)(ctx_);
    }

    void* ctx_;
    T(*getter_)(void*);
};

}

#endif // __FIBRE_PROPERTY_HPP
