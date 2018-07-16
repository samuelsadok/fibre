#ifndef __FIBRE_UUID_HPP
#define __FIBRE_UUID_HPP

#include "crc.hpp" // for std::hash<fibre::Uuid>
#include "cpp_utils.hpp"

#include <stdint.h>
#include <string.h>
#include <functional>

namespace fibre {

/*
* @brief Represents a 16-byte UUID
*/
class Uuid
{
public:

    using data_type = std::array<uint8_t, 16>;
	//Uuid(const uint8_t& bytes[16]);
	//Uuid(const std::string &fromString);
    /*
    * @brief Constructs a random UUID
    * The UUID complies to version 4 variant 1 of RFC
    */
	//Uuid() {
    //    get_random(bytes, 16);
    //}
    Uuid() : bytes_{0} {}
    Uuid(const data_type& bytes)
        : bytes_(bytes) {}
    Uuid(const uint8_t (&bytes)[16])
        : bytes_(detail::to_array(bytes)) {}
    //Uuid(const char (&str)[37])
    //    : Uuid();

    static Uuid zero() {
        return Uuid({0});
    }

    static Uuid from_data(uint32_t time_low,
                          uint16_t time_mid,
                          uint16_t time_hi_and_version,
                          uint16_t clk_seq,
                          const uint8_t node[6]) {
        uint8_t bytes[16];
        write_be(time_low, bytes + 0);
        write_be(time_mid, bytes + 4);
        write_be(time_hi_and_version, bytes + 6);
        write_be(clk_seq, bytes + 8);
        memcpy(bytes + 10, node, 6);
        return Uuid(bytes);
    }

    /** @brief Constructs a Uuid object from a string of the
     * format "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
     * If the format is invalid, an all-zero Uuid is returned.
     **/
    static Uuid from_string(const char * str) {
        uint32_t part1;
        uint16_t part2[3];
        uint8_t part3[6];

        if ((strlen(str) == 36) &&
            (str[8] == '-') && (str[13] == '-') && (str[18] == '-') && (str[23] == '-') &&
            hex_string_to_int<uint32_t>(str + 0, &part1) &&
            hex_string_to_int<uint16_t>(str + 9, &part2[0]) &&
            hex_string_to_int<uint16_t>(str + 14, &part2[1]) &&
            hex_string_to_int<uint16_t>(str + 19, &part2[2]) &&
            hex_string_to_int_arr<uint8_t, 6>(str + 24, part3)) {
            return from_data(part1, part2[0], part2[1], part2[2], part3);
        }

        return zero();
    }

	bool operator==(const Uuid &other) const {
        return bytes_ == other.bytes_;
    }

	bool operator!=(const Uuid &other) const {
        return bytes_ != other.bytes_;
    }

    const data_type& get_bytes() const {
        return bytes_;
    }
	//void swap(Uuid &other);
	//bool isValid() const;

private:
    //uint32_t time_low;
    //uint16_t time_mid;
    //uint16_t time_hi_and_version;
    //uint8_t clk_seq_hi_res;
    //uint8_t clk_seq_lo;
	//uint8_t node[6];
	data_type bytes_; // data stored in big endian order

	// make the << operator a friend so it can access _bytes
	//friend std::ostream &operator<<(std::ostream &s, const Uuid &uuid);
	//friend bool operator<(const Uuid &lhs, const Uuid &rhs);
};

using Guid = Uuid; // Dude, it's the same thing.

__attribute__((unused))
static std::ostream& operator<<(std::ostream& stream, const Uuid& uuid) {
    const uint8_t * buffer = uuid.get_bytes().data();
    size_t length = 16;
    return stream << as_hex(read_be<uint32_t>(&buffer, &length)) << "-"
        << as_hex(read_be<uint16_t>(&buffer, &length)) << "-"
        << as_hex(read_be<uint16_t>(&buffer, &length)) << "-"
        << as_hex(read_be<uint16_t>(&buffer, &length)) << "-"
        << as_hex(buffer[0]) << as_hex(buffer[1]) << as_hex(buffer[2]) << as_hex(buffer[3]) << as_hex(buffer[4]) << as_hex(buffer[5]);
}

}


namespace std {
    // @brief Specialization for std::hash<fibre::Uuid>
	template <>
	struct hash<fibre::Uuid> {
		typedef fibre::Uuid argument_type;
		typedef std::size_t result_type;
		result_type operator()(argument_type const &uuid) const {
            return calc_crc<size_t, 0x12345678>(0x12345678, uuid.get_bytes().data(), 16);
		}
	};
}

#endif // __FIBRE_UUID_HPP
