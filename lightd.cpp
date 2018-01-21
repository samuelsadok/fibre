#include <fibre/protocol.hpp>
#include <fibre/posix_udp.hpp>
#include <stdio.h>
#include <unistd.h>
#include <thread>

float brightness = 0;

void set_brighness(void) {
    printf("set brightness to %.3f\n", brightness);
}

const Endpoint endpoints[] = {
    Endpoint::make_function("set_brighness", set_brighness),
        Endpoint::make_property("brightness", &brightness),
    Endpoint::close_tree()
};
constexpr size_t NUM_ENDPOINTS = sizeof(endpoints) / sizeof(endpoints[0]);

/*
void diep(const char *s) {
    perror(s);
    exit(1);
}
*/

int main() {
    printf("Hello World\n");
    std::thread server_thread(serve_on_udp, endpoints, NUM_ENDPOINTS, 9910);

    for (;;) {
        //printf("brightness: %.3f\n", brightness);
        usleep(500000);
    }
    return 0;
}
