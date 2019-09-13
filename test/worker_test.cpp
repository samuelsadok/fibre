
#include <fibre/worker.hpp>
#include <fibre/timer.hpp>
#include <unistd.h>

using namespace fibre;

int worker_test() {
    printf("testing worker and timer...\n");

    Worker worker;
    if (worker.init() != 0) {
        printf("worker init failed.\n");
        return -1;
    }

    Timer timer;
    if (timer.init(&worker) != 0) {
        printf("timer init failed.\n");
        return -1;
    }

    uint32_t counter = 0;
    auto callback = make_lambda_closure([](uint32_t& cnt) { cnt++; }).bind(counter);
    if (timer.start(100, true, &callback) != 0) {
        printf("timer start failed.\n");
        return -1;
    }

    usleep(1000000);

    if (timer.stop() != 0) {
        printf("timer stop failed.\n");
        return -1;
    }

    if (counter < 8 || counter > 12) {
        printf("counter not as expected.\n");
        return -1;
    }

    if (timer.deinit() != 0) {
        printf("timer deinit failed.\n");
        return -1;
    }
    printf("timer deinit() complete\n");

    if (worker.deinit() != 0) {
        printf("worker deinit failed.\n");
        return -1;
    }

    printf("test succeeded!\n");
    return 0;
}

int main(int argc, const char** argv) {
    if (worker_test() != 0)
        return -1;
    return 0;
}
