#ifndef __FIBRE_POOL_HPP
#define __FIBRE_POOL_HPP

#include <fibre/cpp_utils.hpp>
#include <algorithm>
#include <array>
#include <bitset>
#include <tuple>

namespace fibre {

template<typename T, size_t Size> struct Pool;

template<typename T, size_t Size> struct PoolIterator {
    bool operator==(PoolIterator other) const {
        return (container_ == other.container_) && (bitpos_ == other.bitpos_);
    }
    bool operator!=(PoolIterator other) const {
        return !(*this == other);
    }
    PoolIterator& operator++() {
        bitpos_ = find_next(container_->allocation_table, bitpos_);
        return *this;
    }
    T& operator*() const {
        return container_->content[bitpos_].as_T();
    }
    T* operator->() const {
        return &container_->content[bitpos_].as_T();
    }
    Pool<T, Size>* container_;
    size_t bitpos_;
};

template<typename T, size_t Size> struct Pool {
    using iterator = PoolIterator<T, Size>;

    Pool() = default;
    Pool(const Pool&) = delete;  // since we hand out pointers to the internal
                                 // storage we're not allowed to move a pool

    struct alignas(T) StorageElement {
        uint8_t storage[sizeof(T)];
        T& as_T() {
            return *reinterpret_cast<T*>(this);
        }
    };

    template<typename... TArgs> T* alloc(TArgs&&... args) {
        for (size_t i = 0; i < Size; ++i) {
            if (!allocation_table.test(i)) {
                allocation_table[i] = true;
                // TODO: calling the desctructor here is a bit of a hack because
                // the array initializer already calls the constructor
                // content[i].~T();
                new (&content[i].as_T()) T{args...};
                return &content[i].as_T();
            }
        }
        return nullptr;
    }

    void free(T* ptr) {
        content[index_of(ptr)].as_T().~T();
        allocation_table[index_of(ptr)] = false;
    }

    size_t index_of(T* val) {
        return (StorageElement*)val - content.begin();
    }

    iterator begin() {
        return {this, find_first(allocation_table)};
    }

    iterator end() {
        return {this, Size};
    }

    std::array<StorageElement, Size> content;
    std::bitset<Size> allocation_table;
};

template<typename TKey, typename TVal, size_t Size> struct Map {
    using TItem = std::pair<TKey, TVal>;
    using iterator = typename Pool<TItem, Size>::iterator;

    iterator begin() {
        return pool_.begin();
    }
    iterator end() {
        return pool_.end();
    }

    // TODO: remove (find does the job)
    TVal* get(const TKey& key) {
        for (auto& item : pool_) {
            if (item.first == key) {
                return &item.second;
            }
        }
        return nullptr;
    }

    iterator find(const TKey& key) {
        return std::find_if(begin(), end(),
                            [&](TItem& item) { return item.first == key; });
    }

    template<typename... TArgs> TVal* alloc(const TKey& key, TArgs&&... args) {
        TItem* item =
            pool_.alloc(std::piecewise_construct_t{}, std::tuple<TKey>{key},
                        std::forward_as_tuple(args...));
        if (item) {
            return &item->second;
        } else {
            return nullptr;
        }
    }

    void erase(iterator it) {
        pool_.free(&*it);
    }

    Pool<std::pair<TKey, TVal>, Size> pool_;
};

}  // namespace fibre

namespace std {

template<typename T, size_t Size>
struct iterator_traits<fibre::PoolIterator<T, Size>> {
    typedef std::forward_iterator_tag iterator_category;
};

}  // namespace std

#endif  // __FIBRE_POOL_HPP
