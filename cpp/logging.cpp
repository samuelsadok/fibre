
#include <fibre/logging.hpp>

namespace fibre {

Logger logger{};

Logger* get_logger() {
    return &logger;
}

}
