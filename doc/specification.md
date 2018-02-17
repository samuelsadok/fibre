
# Fibre Protocol Specification v0.0.2 Draft #

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL
NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED",  "MAY", and
"OPTIONAL" in this document are to be interpreted as described in
RFC 2119.

Note that this document does not yet reflect the state of the implementation,
but serves as a draft for near-term changes.

## Overview ##

The Fibre framework can be divided into three abstraction layers: Objects, endpoints and messages.

A **Fibre node** can for instance be a standalone program, a device or even a single processor in a multiprocessor system. Fibre nodes host objects or make use of remote objects or both. When describing specific interactions we will call the Fibre node hosting an object the **server** and the node using the object the **client**.

**Objects** can provide functions, properties and events. Object discovery, function calls, property read/write operations, event subscriptions and similar operations are implemented by writing to **endpoints** on the remote Fibre node. In some cases writing to an endpoint on the server yields output data which is in turn written back to an endpoint on the client.

One or more endpoint write requests are packed or segmented into one or more **messages** and delivered on one or more channels. Channels can be stream oriented, packet oriented, unidirectional, bidirectional and can come with all sorts of unreliabilities.

Fibre nodes can negotiate any message format that suits their communication channel(s), therefore achieving near-optimal communication on a wide range of channel types. By adapting the range of supported message formats and the sophistication of certain heuristics to the available resources, Fibre can run on virtually any system from the most powerful supercomputer down to the cheapest microcontroller.


## Message Formats ##

We will use the terms **sender** and **receiver** to denote the originator respectively the destination of a message. This section mostly deals with the specification of the receiver. It is the sender's responsibility to assemble messages that, when
interpreted by a compliant receiver, are conductive to the sender's goals. [TODO: certain restrictions on the sender may be needed to allow future extensibility]

At a given point in time on a given incoming channel, a receiver may be able to accept messages of multiple different formats.
We will call these formats **active message formats**. A Fibre message format can be described as a sequence of **decoders**. Each decoder consumes a certain number of bytes from the incoming byte stream, does something with the bytes and then
passes control to the next decoder which will consume the next incoming bytes and so on. A decoder may also terminate, in which case the receiver MUST go back to idle mode.

When a receiver is in idle mode and receives the prefix of any of the active message formats, it passes the subsequent bytes to the corresponding sequence of decoders, thereby interpreting the message. For packet based channels, a prefix can only be placed at the beginning of a packet. The receiver MUST ignore prefixes that occur in other positions of the packet.

Decoders can also be nested inside other decoders. This is useful if for instance the input stream needs to be decrypted, validated or otherwise altered/augmented before it can be passed to the inner decoders. If an inner decoder terminates the outer decoder may or may not escalate the termination. For details refer to each decoder's documentation.


### State Variables ###

The specification of the decoders makes use of variables that capture the state of the receiver at a certain point of decoding a message. For instance one decoder may read a few bytes from the input stream to set a state variable which then influences the behavior of a later decoder.

If a variable is used without being assigned a value previously during processing the same message, it MUST be considered to hold the default value noted in the table below. Note that the descriptions provided here are only of informative nature. When in doubt their precise usage shall be inferred from the decoder specifications.

| Name | Default | Description |
|------|---------|-------|
| `EndpointId` | 0 | defines the endpoint ID to which the payload should be written |
| `Length` | 0 | defines the length of the payload |
| `MTU` | >= 4 | maximum number of remaining bytes [TODO: usage unclear, probably remove] |
| `ExpectAck` | false | whether the sender expects an acknowledgement for the payload |
| `AckEndpoint` | 0 | the endpoint where to send an acknowledgement for the payload |
| `ExpectResponse` | false | whether the sender expects a response from processing the payload |
| `ResponseEndpoint` | 0 | the endpoint where to send the response from processing the payload |
| `SeqNo` | 0 | the sequence number of the payload ID of the sequence number counter that should be incremented after processing the payload |
| `SeqNoThread` | 0 | acts as a namespace for the sequence number so that multiple independent counters can be used in parallel |
| `EnforceOrdering` | false | if true, the payload will not be processed until all other payloads of the same thread with lower sequence numbers have been processessed |

### Decoders ###

This section describes the official Fibre decoders, some of which are optional for a receiver to implement. Apart from these, Fibre nodes MAY implement other, unofficial decoders. [TODO: add namespace to avoid clashes with future official decoders]

Note that some decoders come in multiple variants that are functionally the same but interpret the input stream in a slightly different way. For example `EndpointId:[uint-type]` means that `EndpointId:varint` and `EndpointId:uint16_le` are both valid decoder names. The `uint16_le` variant reads two bytes from the input stream and interprets them as a little endian unsigned integer. The meaning of analogous format names should be clear. The format `varint` is to be interpreted as defined in [Google Protocol Buffers](https://developers.google.com/protocol-buffers/docs/encoding#varints). [TODO: maybe the format should not be part of the name, but rather an attribute accompanying the name]

  - `EndpointId:[uint-type]`
    Reads an unsigned integer from the input stream and assigns it to `EndpointId`. [TODO: probably we should allow hierarchical endpoints, like 5->2->2->3]

  - `Length:[uint-type]`
    Reads an unsigned integer from the input stream and assigns it to `Length`.

  - `SeqNo:[uint-type]`
    Reads _two_ unsigned integers from the input stream and assigns them to `SeqNoThread` and `SeqNo` (in this order). [TODO: impose limits on the number of SeqNo threads] [TODO: discuss rollover of sequence numbers]

  - `Flags`
    Reads one byte from the input stream and does the following assignments:
    [TODO: might make sense to merge this into the `SeqNo` decoder]
     - Bit 0 -> `EnforceOrder`
     - Bit 1 -> `ExpectAck`
     - Bit 2 -> `ExpectResponse`
  
  - `Payload`
    This decoder represents the actual endpoint write request. The decoder MUST consume `Length` bytes and then increment `EndpointId` as well as `SeqNo` by one. Executing this decoder is equivalent to receiving an endpoint write request in the context of the state variables at the time the decoder consumes the bytes. The semantics of receiving requests are described in detail in [Request/Response dialogue](#request-response-dialogue).

  - `Crc8Interleaved:[polynomial],[init]` (nested decoder)

    `[polynomial]` and `[init]` are both a 2-digit lower case hexadecimal number.

    This decoder interprets the incoming byte stream as a sequence of 4 byte blocks, each consisting of 3 data bytes followed
    by one CRC8 checksum byte. The CRC init value for each block is the CRC8 result of the previous block.  The CRC init value of the first block
    is `[init]`.

    The decoder removes the CRC bytes from the stream and passes the data bytes to the inner decoders except if an invalid CRC is found, in which case it aborts.

```
Input stream:
+--------+--------+--------+------------+--------+--------+--------+------------+
| byte 1 | byte 2 | byte 3 | crc(1...3) | byte 4 | byte 5 | byte 6 | crc(4...5) | ...
+--------+--------+--------+------------+--------+--------+--------+------------+
becomes:
+--------+--------+--------+--------+--------+--------+
| byte 1 | byte 2 | byte 3 | byte 4 | byte 5 | byte 6 | ...
+--------+--------+--------+--------+--------+--------+
```

  - `Repeat:[uint-type]` (nested decoder)
  
    Reads an unsigned integer from the input stream. The inner decoders are then looped as often as specified by this integer. This decoder terminates if an inner decoder terminates.

[TODO: include a way to ensure graceful failure if the session is reset on one side]

## Request/Response dialogue ##

Requests come in the form of bytes that are to be written to one of the server's endpoints, specifically the one denoted by `EndpointId`. **Processing** the request is the act of the server passing the data to the appropriate internal endpoint handler. This may yield one or more bytes of result. If `ExpectResponse` is true, the result is returned in the form of a write request to one of the client's endpoints, specifically the one denoted by `ResponseEndpoint`. [TODO: specify how the outgoing channel for the response should be selected]

### Preconditions ###

A receiver MUST NOT process a request before its precondition is met. The precondition is:

`EnforceOrdering` is false _or_ all requests with tuples (`SeqNoThread`, `x`), `x` < `SeqNo` have already been fully processed.

### Unique requests ###

For simplicity we will first assume that there are no duplicate requests.

Upon receiving a unique request that wants an acknowledgement, the server MUST choose one of three options:
 1. Send back a NACK. In this case the server MUST NOT process the request.
 2. Send back an ACK. In this case, as long as the applicable limits are not are not exceeded, the server MUST (in this order):
    1. Buffer the request until the precondition is met
    2. Process the request
    3. Ensure the delivery of the result. Note that reliable delivery of the result can be ensured either by choosing a reliable channel or by requesting an ACK for the result.
    
    Applicable limits: Point 1 is subject to the input buffering limit, point 3 is subject to the output buffering limit and points 1-3 are subject to the request timeout limit.
 3. Skip the ACK in case the sender expects a result. In this case, as long as the applicable limits are not are not exceeded, the server MUST (in this order):
    1. Buffer the request until the precondition is met
    2. Process the request
    3. Deliver the result. The result MAY be sent unreliably.
    
    Note that if the result is sent unreliably, the server may need to buffer the result to comply to point 2 in [Duplicate Requests](#duplicate-requests). Such a buffering would be subject to the output buffering limit.

TODO: restrict the circumstances under which a NACK is allowed
TODO: discuss requests that expect _no_ acknowledgement (does expect result imply expect ACK?)

### Duplicate Requests ###

If the same request (as identified by the (`SeqNoThread`, `SeqNo`) tuple) is received multiple times, the requirements for the server are in general the same as for unique requests, with two differences:

**Dropping selected requests:**
The server is allowed to completely ignore _some_ of the duplicates as long as the following statement holds true: If the number of duplicates goes to infinity, the probability of a successful delivery of at least one ACK, NACK or result (whichever is applicable) must approach 1.
  
As an example we give three policies:
  1. Handle _all_ incoming requests as specified in [Unique Requests](#unique-requests).
  2. Handle request copies 1, 3, 6, 11, 20, 37, ... and ignore all in between (aka exponential backoff).
  3. Only handle the first copy and then ignore all subsequent duplicates.

Examples 1 and 2 are both compliant, however example 1 may use more bandwidth than necessary. Example 3 is compliant if and only if the result or NACK is returned over a reliable channel.

**Avoiding duplicate actions:** Let a **pure** request be a request that, when processed multiple times, has the same effect (but not necessarily result) as a when processed one single time. [TODO: not sure if this is precise enough] Duplicate write requests that are not pure MUST NOT be processed more than once.


Example:
```
  client                                server
    |                                     |
    |    "SeqNo7: write to endpoint 1"    |
    |  ================================>  |
    |           "ACK for SeqNo7"          |
    |  <================================  |
    |    "SeqNo7: write to endpoint 1"    |
    |  ================================>  | detect duplicate
    |    "SeqNo7: write to endpoint 1"    |
    |  ================================>  | detect duplicate
    |           "ACK for SeqNo7"          |
    |  <================================  |
    |                                     |
    |                                     | wait for preconditions
    |                                     | process request
    |   "SeqNo5: result for SeqNo5: ..."  |
    |  <================================  |
    |           "ACK for SeqNo5"          |
    |  ================================>  |
    |   "SeqNo5: result for SeqNo5: ..."  |
    |  <================================  |
    |           "ACK for SeqNo5"          |
    |  ================================>  |
```



### Message Format Negotiation ###

A Fibre receiver MUST support the following message format and associate it with the prefix `0xAA`:

```
"Crc8Interleaved:37,42": [
  "EndpointId:varint"
  "Length:varint"
  "Payload"
]
```

Apart from this mandatory format, there exist the following three mechanisms for the sender to gain knowledge about the receiver's supported message formats:
 - Prior knowledge, for instance if both nodes are from the same vendor.
 - The sender requests the information from by writing to the receiver's endpoint 0. [TODO: describe]
 - The sender proposes a message format and hopes that the receiver will accept it. [TODO: describe]
   The sender MAY start using the message format right away, however it SHOULD expect such
   messages to be rejected.

**How to request supported message formats:**

The sender sends a token to the receiver. [TODO: maybe a `SenderId` on a message level is appropriate]

The receiver responds with a dictionary (map) in [MessagePack](https://msgpack.org/index.html) format [TODO: restrict features of MessagePack]. This dictionary contains the following keys:

 - `token`: REQUIRED
    Contains the token that was sent in the request

 - `formats`: REQUIRED
    A list of message-format objects. Each object is a map containing the following keys:
    - `prefix`: Binary data specifying the prefix
    - `decoders`: A list of decoder specs
      Simple decoders are specified by a string stating their name.
      Decoders with inner decoders can be either a string specifying the name of the decoder or a map containing the following keys:
      - `name`: The name of the decoder
      - `inner`: A list of decoder specs

 - `mtu`: REQUIRED
    Indicates the maximum length of a message.

 - `max-varint`: REQUIRED
    Binary data containing the largest varint that this node is able to parse.

 - `format-caps`: OPTIONAL
    A map containing information about the message formats that this receiver is able to accept.
    - `decoders`:
        A list of strings specifying the supported decoders.
    - `max-format-count`:
        The maximum number of message formats that this receiver is able to accept.
    - `max-format-length`:
        The maximum length of each occurrence of the `decoders` list
    - `max-format-depth`:
        The maximum depth of the decoder tree. A depth of 1 means that no nested decoders are allowed.

[TODO: include recommendations for common channel types: TCP, UDP, UART, USB bulk, CAN ...]

## More TODO's ##

 - The sender needs to know the receiver's MTU for early operations during handshake. Maybe we don't need an MTU. Find an example where we need one.
 - Think about channel association

    Example:
```
sender: GetInformation(token: ABC, channel_hints: ...) <= sender gives hints about how it may be reachable, e.g. IP addresses
receiver on out1 channel1: token + "1"
receiver on out2 channel2: token + "2" <= receiver tries to respond on multiple channels
sender: ConfirmChannels(token + "1", token + "2") <= sender tells the receiver which responses got through, so the next response is more efficient
```

 - Think about how to split and reassemble a large endpoint write requests
 - The naming conventions in this specification are up for discussion

Before considering the protocol stable we want to meet the following design goals

 - Ensure it works on a bus like CAN
 - Run on an ATtiny5 with 32B RAM and 512B program memory
 - Resilience against DoS targeting a Fibre device
 - Resilience against DoS abusing a Fibre device as a work multiplier

And have a general outline for the following:

 - Encryption/Authentication/Priviledges
 - How does relaying work

