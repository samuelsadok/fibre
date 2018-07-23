## Overview ##

The goal of Fibre is to provide a framework to make suckless distributed applications easier to program.

In particular:

 - Nobody likes boiler plate code. Using a remote object should feel almost
   exactly as if it was local. No matter if it's in a different process, on
   a USB device, connected via Bluetooth, over the Internet or all at the
   same time.
   All complexity arising from the system being distributed should be taken
   care of by Fibre while still allowing the application developer to easily
   fine-tune things.

 - Fibre has the ambition to run on most major platforms and provide bindings
   for the most popular languages. Even bare metal embedded systems with very
   limited resources. See the Compatibility section for the current status.

 - Once you deployed your application and want to change the interface, don't
   worry about breaking other applications. With Fibre's object model, the
   most common updates like adding methods, properties or arguments won't
   break anything. Sometimes you can get away with removing methods if they
   weren't used by other programs. **This is not implemented yet.**

## Current Status ##

Most aspects of Fibre are being reworked for v0.2.0.
[Technical Specification](#technical-specification) depicts an accurate outline of the rework.

Currently you can publish a function in C++ like this:

```cpp
void my_function(uint32_t input1, uint32_t& output1) {
    [...]
}

FIBRE_EXPORT_FUNCTION(my_function, INPUTS("input1"), OUTPUTS("output1"));

void main() {
    fibre::init()
}
```

Any number of input and output values are supported. Only a few types are supported though.

See `test/test_server.cpp` for details.

`tools/fibre-shell` will connect to the test_server, instantiate a `RemoteNode` and invoke `_interrogate` on that instance. For testing purposes you can put code there to invoke remote functions.

### Changes from v0.1.0 to v0.2.0 ###

 * Support for multiple channels per remote node
 * Functions can have multiple input/output arguments and theoretically unlimited input/output data size
 * One function call corresponds to one endpoint operation and can be sent in a single message.
 * Each function has an independent JSON snippet, allowing for adding functions dynamically at runtime
 * Same for types (not implemented yet)

### TODO for v0.2.0: ###

 - [ ] [general] Return ACKs/NACKs to make functions without return values work.
 - [ ] [C++] add resend timer like in python (see `remote_node.py` vs `remote_node.cpp`)
 - [ ] [C++] Return the proper JSON snippet for each function signature (currently a "demo"-string is built). See `FunctionJSONAssembler`.
 - [ ] [C++] Publish object reference types similar to how functions are published and build a constexpr JSON string that describes each type.
 - [ ] [C++] Garbage collect RemoteNodes that are no longer in active contact (how?)
 - [ ] [C++] Replace all std::vector and std::unordered_map by statically sized alternatives (with a config option).
 - [ ] [Python] Dynamically build objects from JSON
 - [ ] [C++/Python] make USB, UDP and UART work again


## Show me some code

**This section is not up-to-date**

**TODO: move this to the respective language repos**

Consider this program:

```
class TestClass {
public:
    float property1;
    float property2;

    float set_both(float arg1, float arg2) {
        property1 = arg1;
        property2 = arg2;
        return property1 + property2;
    }
};


int main() {
    TestClass test_object = TestClass();

    while (1) {
        printf("test_object.property1: %f\n", test_object.property1);
        usleep(1000000 / 5); // 5 Hz
    }
}
```

Say you want to publish `test_object` so that a remote Fibre node can use it.

1. Add includes
      ```C++
      #include <fibre/protocol.hpp>
      #include <fibre/posix_tcp.hpp>
      ```
2. Add Fibre export definitions to the exported class
      ```C++
      class TestClass {
            [...]
            FIBRE_EXPORTS(TestClass,
                  make_fibre_property("property1", &property1),
                  make_fibre_property("property2", &property2),
                  make_fibre_function("set_both", *obj, &TestClass::set_both, "arg1", "arg2")
            );
      };
      ```
   Note: in the future this will be generated from a YAML file using automatic code generation.

3. Publish the object on Fibre
      ```C++
      auto definitions = test_object.fibre_definitions;
      fibre_publish(definitions);
      ```
   Note: currently you must publish all objects at once. This will be fixed in the future.

4. Start the TCP server
      ```C++
      std::thread server_thread_tcp(serve_on_tcp, 9910);
      ```
      Note: this step will be replaced by a simple `fibre_start()` call in the future. All builtin transport layers then will be started automatically.

## Adding Fibre to your project ##

We recommend Git subtrees if you want to include the Fibre source code in another project.
Other contributors don't need to know anything about subtrees, to them the Fibre repo will be like any other normal directory.

#### Adding the repo
```
git remote add fibre-origin git@github.com:samuelsadok/fibre.git
git fetch fibre-origin
git subtree add --prefix=fibre --squash fibre-origin master
```
Instead of using the upstream remote, you might want to use your own fork for greater flexibility.

#### Pulling updates from upstream
```
git subtree pull --prefix=fibre --squash fibre-origin master
```

#### Contributing changes back to upstream
This requires push access to `fibre-origin`.
```
git subtree push --prefix=fibre fibre-origin master
```

## Technical Specification ##

### Low Level ###

Fibre Nodes are interconnected through any number and any type of unidirectional or bidirectional channels:

![network diagram](doc/pics/network.png)

_(Currently Fibre nodes can only communicate when there is a direct channel between them. Support for thin and thick relays will be added later, see future concepts)_

Between any two actively interacting nodes, one or more bidirectional pipes are establised:

![pipe diagram](doc/pics/pipes.png)

Pipes guarantee that the data inserted on one side arrives reliably on the other side, unless the pipe breaks, in which case the sender will know about it _(we may add support for less reliable pipes later)_. Furthermore pipes support packet breaks that can be used by the sender to signal the end of a packet.

Since a node needs to allocate local resources (e.g. pipe buffers) for every remote node that it wants to talk to, the number of concurrent communication parners of a certain node may be limited and may be as low as one. Likewise, the number of concurrent pipes per remote node may be limited to one.

The multiplexer is responsible of checking the output pipes for pending data and choose wisely on which output channel to emit which chunks of data. This scheduling algoritm may depend on several factors, for instance pipe priorities, channel bandwidth and channel latency. If lossy channels are used the scheduler may further have to resend unacknowledged chunks after some time.

The exact data format may vary depending on the channel type but currently for TCP it looks like this:

```
+----------+-------+-------+-------+-----
| own UUID | chunk | chunk | chunk | ...
+----------+-------+-------+-------+-----
```

where each chunk looks like this:
```
+---------+--------------+--------------+---------+
| pipe ID | chunk offset | chunk length | payload |
+---------+--------------+--------------+---------+
```

Each field is a 16 bit little endian unsigned integer. Chunk offset describes where in the pipe's stream this chunk belongs. The chunk length is the number of bytes in the payload, left-shifted by one. The LSB is the "packet break" bit, 1 indicating that a packet break should be inserted at the end of this chunk.

_(currently there is an additional CRC field in the chunk header. It's 0x1337 at offset 0 and from there tracks the CRC of the pipe stream. Not sure if we should keep that)_

### High Level ###

A Fibre node can implement one or two of the following roles:

* **Server:** exposes local functions and publishes local objects.
* **Client:** invokes remote functions and makes use of remote objects.

To call a function on a given remote node, the server does the following:
1. Select an inactive pipe.
2. Send the desired function's ID and hash value on the pipe.
3. Serialize the function arguments onto the pipe, one by one.
4. Send a packet break on the pipe.
5. On the corresponding input pipe, deserialize the function's results. The client should also terminate the result with a packet break.

This requires prior mutual agreement on several things, e.g. function ID, function inputs/outputs and their raw data formats. Therefore Fibre defines several canonical functions that can be used by the server to interrogate the client about other exposed functions.

**to be continued ...**

### Future Concept: Relays ###

**Thin Relays** expose their own channels as endpoints and thereby allow other clients to use their channels to communicate with a more distant node. This allows multi-hop networking across nodes with very limited resources.

**Thick Relays** transparently collapse the complete network layout into one flat node. All functions and objects of remote nodes are advertised to adjacent nodes as if they were local to the the relay. This allows nodes with limited resources to communicate with a larger part of the network.
On a desktop PC, it also allows to concentrate all resources of Fibre in a single hub service, such that multiple user programs can plug in easily with minimal additional resources.


## Projects using Fibre ##

 - [ODrive](https://github.com/madcowswe/ODrive): High performance motor control
 - [lightd](https://github.com/samuelsadok/lightd): Service that can be run on a Raspberry Pi (or similar) to control RGB LED strips

## Contribute ##

This project losely adheres to the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
