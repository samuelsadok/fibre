
#if defined(_WIN32) || defined(_WIN64)
#include <fibre/windows_worker.hpp>
//#include <fibre/windows_timer.hpp>
#define usleep(arg)  ((void)(arg))
#endif

#if defined(__linux__)
#include <fibre/linux_worker.hpp>
#include <fibre/linux_timer.hpp>
#include <unistd.h>
#endif

#include "test_utils.hpp"

using namespace fibre;

template<typename TWorker, typename TTimer>
TestContext timer_test() {
    TestContext context;
    printf("testing worker and timer...\n");

    TWorker worker;
    TEST_ZERO(worker.init());

    TTimer timer;
    TEST_ZERO(timer.init(&worker));

    uint32_t counter = 0;
    auto callback = make_lambda_closure([](uint32_t& cnt) { cnt++; }).bind(counter);
    TEST_ZERO(timer.start(100, true, &callback));

    usleep(1000000);

    TEST_ZERO(timer.stop());

    std::cout << "counter: " << counter << std::endl;
    TEST_ASSERT(counter > 8 && counter < 12);

    TEST_ZERO(timer.deinit());
    TEST_ZERO(worker.deinit());

    return context;
}

int main(int argc, const char** argv) {
    TestContext context;

#if defined(__linux__)
    TEST_ADD((timer_test<LinuxWorker, LinuxTimer>()));
#endif

#if defined(_WIN32) || defined(_WIN64)
#endif

    return context.summarize();
}
