#ifndef __FIBRE_BACKPORT_OPTIONAL_HPP
#define __FIBRE_BACKPORT_OPTIONAL_HPP

#if __cplusplus >= 201703L
#include <optional>
#else

namespace std {

/// Tag type to disengage optional objects.
struct nullopt_t {
  // Do not user-declare default constructor at all for
  // optional_value = {} syntax to work.
  // nullopt_t() = delete;

  // Used for constructing nullopt.
  enum class _Construct { _Token };

  // Must be constexpr for nullopt_t to be literal.
  explicit constexpr nullopt_t(_Construct) { }
};

constexpr nullopt_t nullopt { nullopt_t::_Construct::_Token };

template<typename T>
class optional {
public:
    using storage_t = char[sizeof(T)];

    optional() : has_value_(false) {}
    optional(nullopt_t val) : has_value_(false) {}

    optional(const optional & other) : has_value_(other.has_value_) {
        if (has_value_)
            new ((T*)content_) T{*(T*)other.content_};
    }

    optional(optional&& other) : has_value_(other.has_value_) {
        if (has_value_)
            new ((T*)content_) T{*(T*)other.content_};
    }

    optional(T& arg) {
        new ((T*)content_) T{arg};
        has_value_ = true;
    }

    optional(T&& arg) {
        new ((T*)content_) T{std::forward<T>(arg)};
        has_value_ = true;
    }

    ~optional() {
        if (has_value_)
            ((T*)content_)->~T();
    }

    inline optional& operator=(const optional & other) {
        (*this).~optional();
        new (this) optional{other};
        return *this;
    }

    inline bool operator==(const optional& rhs) const {
        return (!has_value_ && !rhs.has_value_) || (*(T*)content_ == *(T*)rhs.content_);
    }

    inline bool operator!=(const optional& rhs) const {
        return !(*this == rhs);
    }

    inline T& operator*() {
        return *(T*)content_;
    }

    inline T* operator->() {
        return (T*)content_;
    }

    size_t has_value() const { return has_value_; }

    inline T& value() {
        return *(T*)content_;
    }

    inline const T& value() const {
        return *(T*)content_;
    }

    alignas(T) storage_t content_;
    size_t has_value_;
};

template<typename T>
optional<T> make_optional(T&& val) {
    return optional<T>{std::forward<T>(val)};
}

template<typename T>
optional<T> make_optional(T& val) {
    return optional<T>{val};
}

}

#endif

#endif // __FIBRE_BACKPORT_OPTIONAL_HPP
