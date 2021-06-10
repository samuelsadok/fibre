#ifndef __FIBRE_INTERFACE_HPP
#define __FIBRE_INTERFACE_HPP

#include <fibre/base_types.hpp>
#include <fibre/rich_status.hpp>
#include <string>
#include <vector>

namespace fibre {

class Interface;
struct InterfaceInfo;
struct Object;
class Function; // defined in function.hpp

struct AttributeInfo {
    std::string name;
    Interface* intf;
};

struct InterfaceInfo {
    std::string name;
    std::vector<Function*> functions;
    std::vector<AttributeInfo> attributes;
};

class Interface {
public:
    Interface() {}
    Interface(const Interface&) = delete; // interfaces must not move around in memory

    virtual InterfaceInfo* get_info() = 0;
    virtual void free_info(InterfaceInfo* info) = 0;
    virtual RichStatusOr<Object*> get_attribute(Object* parent_obj, size_t attr_id) = 0;
};

}

#endif // __FIBRE_INTERFACE_HPP
