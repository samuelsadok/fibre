
#include <stdio.h>
#include <unistd.h>
#include <thread>
#include <signal.h>

#include <fibre/protocol.hpp>
#include <fibre/posix_udp.hpp>


// global variable available on our testObject
int testProperty = 0;

/* Fibre endpoint definitions ------------------------------------------------*/
// TODO: This whole section is horrible boilerplate code. Autogenerate it.

const Endpoint endpoints[] = {
    Endpoint::make_object("testobject"),
        Endpoint::make_property("testProp", &testProperty),
    Endpoint::close_tree()
};
constexpr size_t NUM_ENDPOINTS = sizeof(endpoints) / sizeof(endpoints[0]);

/*----------------------------------------------------------------------------*/



static int running = 1;
static void sigterm_handler(int signum) {
	(void)(signum);
    running = 0;
}


int main() {
    printf("Starting __fibre_test server...\n");


    // set up terminate-signals
    struct sigaction sa;
    sa.sa_handler = sigterm_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);



    // expose service on Fibre
    std::thread server_thread(serve_on_udp, endpoints, NUM_ENDPOINTS, 9910);

    printf("__fibre_test server started.\n");

    while (running) {
        // let the driver output the colors
        printf("testProperty: %i", testProperty);

        // 10 frames / sec
        usleep(1000000 / 10);
    }

    return 0;
}
