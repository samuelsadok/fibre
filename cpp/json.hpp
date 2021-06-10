#ifndef __FIBRE_JSON_HPP
#define __FIBRE_JSON_HPP

#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <fibre/logging.hpp>
#include <fibre/backport/variant.hpp>

struct json_error {
    const char* ptr;
    std::string str;
};

struct json_value;
using json_list = std::vector<std::shared_ptr<json_value>>;
using json_dict = std::vector<std::pair<std::shared_ptr<json_value>, std::shared_ptr<json_value>>>;
using json_value_variant = std::variant<std::string, int, json_list, json_dict, json_error>;

struct json_value : json_value_variant {
    //json_value(const json_value_variant& v) : json_value_variant{v} {}
    template<typename T> json_value(T&& arg) : json_value_variant{std::forward<T>(arg)} {}
    //json_value_variant v;
};

// helper functions
inline bool json_is_str(json_value val) { return val.index() == 0; }
inline bool json_is_int(json_value val) { return val.index() == 1; }
inline bool json_is_list(json_value val) { return val.index() == 2; }
inline bool json_is_dict(json_value val) { return val.index() == 3; }
inline bool json_is_err(json_value val) { return val.index() == 4; }
inline std::string json_as_str(json_value val) { return std::get<0>(val); }
inline int json_as_int(json_value val) { return std::get<1>(val); }
inline json_list json_as_list(json_value val) { return std::get<2>(val); }
inline json_dict json_as_dict(json_value val) { return std::get<3>(val); }
inline json_error json_as_err(json_value val) { return std::get<4>(val); }

inline json_value json_make_error(const char* ptr, std::string str) {
    return {json_error{ptr, str}};
}


inline void json_skip_whitespace(const char** begin, const char* end) {
    while (*begin < end && std::isspace(**begin)) {
        (*begin)++;
    }
}

inline bool json_comp(const char* begin, const char* end, char c) {
    return begin < end && *begin == c;
}

inline json_value json_parse(const char** begin, const char* end, fibre::Logger logger) {
    // skip whitespace

    if (*begin >= end) {
        return json_make_error(*begin, "expected value but got EOF");
    }

    if (json_comp(*begin, end, '{')) {
        // parse dict
        (*begin)++; // consume leading '{'
        json_dict dict;
        bool expect_comma = false;

        json_skip_whitespace(begin, end);
        while (!json_comp(*begin, end, '}')) {
            if (expect_comma) {
                if (!json_comp(*begin, end, ',')) {
                    return json_make_error(*begin, "expected ',' or '}'");
                }
                (*begin)++; // consume comma
                json_skip_whitespace(begin, end);
            }
            expect_comma = true;

            // Parse key-value pair
            json_value key = json_parse(begin, end, logger);
            if (json_is_err(key)) return key;
            json_skip_whitespace(begin, end);
            if (!json_comp(*begin, end, ':')) {
                return json_make_error(*begin, "expected :");
            }
            (*begin)++;
            json_value val = json_parse(begin, end, logger);
            if (json_is_err(val)) return val;
            dict.push_back({std::make_shared<json_value>(key), std::make_shared<json_value>(val)});

            json_skip_whitespace(begin, end);
        }

        (*begin)++;
        return {dict};

    } else if (json_comp(*begin, end, '[')) {
        // parse list
        (*begin)++; // consume leading '['
        json_list list;
        bool expect_comma = false;

        json_skip_whitespace(begin, end);
        while (!json_comp(*begin, end, ']')) {
            if (expect_comma) {
                if (!json_comp(*begin, end, ',')) {
                    return json_make_error(*begin, "expected ',' or ']'");
                }
                (*begin)++; // consume comma
                json_skip_whitespace(begin, end);
            }
            expect_comma = true;

            // Parse item
            json_value val = json_parse(begin, end, logger);
            if (json_is_err(val)) return val;
            list.push_back(std::make_shared<json_value>(val));

            json_skip_whitespace(begin, end);
        }

        (*begin)++; // consume trailing ']'
        return {list};

    } else if (json_comp(*begin, end, '"')) {
        // parse string
        (*begin)++; // consume leading '"'
        std::string str;

        while (!json_comp(*begin, end, '"')) {
            if (*begin >= end) {
                return json_make_error(*begin, "expected '\"' but got EOF");
            }
            if (json_comp(*begin, end, '\\')) {
                return json_make_error(*begin, "escaped strings not supported");
            }
            str.push_back(**begin);
            (*begin)++;
        }

        (*begin)++; // consume trailing '"'
        return {str};

    } else if (std::isdigit(**begin)) {
        // parse int

        std::string str;
        while (*begin < end && std::isdigit(**begin)) {
            str.push_back(**begin);
            (*begin)++;
        }

        return {std::stoi(str)}; // note: this can throw an exception if the int is too long

    } else {
        return json_make_error(*begin, "unexpected character '" + std::string(*begin, *begin + 1) + "'");
    }
}

inline json_value json_dict_find(json_dict dict, std::string key) {
    auto it = std::find_if(dict.begin(), dict.end(),
        [&](std::pair<std::shared_ptr<json_value>, std::shared_ptr<json_value>>& kv){
            return json_is_str(*kv.first) && json_as_str(*kv.first) == key;
        });
    return (it == dict.end()) ? json_make_error(nullptr, "key not found") : *it->second;
}

#endif // __FIBRE_JSON_HPP
