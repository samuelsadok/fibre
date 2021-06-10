#ifndef __FIBRE_CAN_BACKEND_HPP
#define __FIBRE_CAN_BACKEND_HPP

#include "../interfaces/canbus.hpp"
#include <fibre/channel_discoverer.hpp>
#include <fibre/connection.hpp>
#include <fibre/../../mini_rng.hpp>
#include <fibre/low_level_protocol.hpp>
#include <fibre/node.hpp>
#include <fibre/pool.hpp>

namespace fibre {

class TimerProvider;
class Timer;

struct CallContext {
    uint8_t protocol;
    bool protocol_known = true;
    ReceiverState state;

    uint8_t routing_info[17];
    size_t routing_info_offset;

    uint16_t frame_ids[kMaxLayers];
    size_t n_layers = 0;

    ConnectionInputSlot* handler = nullptr;

    void reset_at(Domain* domain, uint8_t layer);
};

/**
 * @brief
 * 
 * 
 * 
 * # Node ID autoconfig
 * 
 * This section describes the Node ID (0) autoconfig algorithm used by
 * Fibre-on-CAN. It is loosely inspired by https://ieeexplore.ieee.org/document/7993257.
 * It has the following properties:
 *  - all nodes have equal roles, there is no master
 *  - nodes can be started and restarted in any order
 *  - networks can be partitioned and joined at any time in any way
 *  - there is some fuzzyness: messages can be delivered to the wrong node for
 *    some time. However this is equivalent to fuzzyness from other sources
 *    (reboot in combination with temporary disconnect) and therefore detected
 *    and handled on a Fibre protocol level.
 * 
 * 
 * ## State Machine
 * 
 * Each node starts in RESTRAINED state and transitions between RESTRAINED and
 * OPERATIONAL state according to the following rules.
 * 
 * RESTRAINED:
 * 
 * In this state the node does not send "Node ID guard" messages and does not
 * send/receive any application level messages (1).
 * 
 *   1. Select next random node ID (see [Randomness](#randomness))
 *   2. Send "NodeID acquisition" message in one-shot mode (without resend-on-NACK).
 *      - arbitration fails => repeat step 3 (2)
 *      - data collision => go to step 1 (3)
 *      - ack'd => go to step 4 (4)
 *      - nack'd => accept tentative Node ID and transition to OPERATIONAL state (5)
 *   3. Wait for 100ms (6)
 *   4. Send "NodeID acquisition" message in one-shot mode (without resend-on-NACK). (7)
 *      - arbitration fails => repeat step 4 (2)
 *      - data collision => go to step 1 (3)
 *      - ack'd or nack'd => accept tentative Node ID and transition to
 *        OPERATIONAL state
 * 
 * If during any of this time a "Node ID acquisition", "Node ID guard" or
 * application-level messages with the own tentative node ID is received, the
 * node must fall back to step 1 (8). Optionally it can abort the already enqueued
 * "Node ID acquisition message" (9).
 * 
 * OPERATIONAL:
 * 
 * Application-level messages are sent and accepted normally using the current
 * NodeID.
 * If a "Node ID acquisition" message is seen for the node's current Node ID, it
 * defends its node ID by responding with a "Node ID guard" message.
 * 
 * The node abandons the current Node ID and transitions to RESTRAINED state in
 * any of the following cases:
 *   - An application-level or "Node ID guard" message is seen with the own Node
 *     ID. (10)
 *   - Transmission fails due to a data collision 3 times in a row. (11)
 * 
 * 
 * ## Randomness
 * 
 * Each node needs 16 bytes of uniformly distributed randomness which may or may
 * not be constant across reboots. That means it can be derived from the device's
 * serial number but in that case it should be digested by a hash or encryption
 * scheme to satisfy the uniformity constraint. Node ID acquisition messages
 * contain a verbatim copy of this 16 byte randomness.
 * 
 * The Node ID selection sequence is a (pseudo-)random sequence of uniformly
 * distributed bytes. If it is pseudorandom, it must use the aformentioned 16-
 * byte source of randomness as seed (or some of it) (12).
 * 
 * 
 * ## Messages
 * 
 *  - Node ID acquisition message:
 *      - Message ID (MSB to LSB): 20-bit prefix, 0b1, 8-bit NodeID
 *      - Data: 16-byte randomness (see [Randomness](#randomness)). (13)
 *  - Node ID guard message:
 *      - Message ID (MSB to LSB): 20-bit prefix, 0b0, 8-bit NodeID (14)
 *      - Data: none
 *  - Application level message:
 *      - Message ID (MSB to LSB): 21-bit application-defined number, 8-bit sender NodeID (15)
 *        (the application-defined number may or may not contain a receiver NodeID)
 *      - Data: application defined
 * 
 * Where "20-bit prefix" stands for 0b1111'0101'0101'0101'0101.
 * 
 * 
 * ## Design Rationale
 * 
 *  (0) The meaning of Node ID should be made clear here: It primarily serves
 *      the purpose of bus access arbitration in a network of multiple
 *      unsynchronized nodes. Identification of the sender/receiver is only a
 *      secondary use and therefore ok to be fuzzy.
 *  (1) This prevents new nodes that happen to select an already taken Node ID
 *      from generating unnecessary Node ID churn on the bus.
 *  (2) Normal condition where two unrelated nodes send at the same time.
 *  (3) Two new nodes tried to acquire the same node ID simultaneously. It is
 *      unclear which should win, therefore they must both try a new node ID.
 *  (4) The acquisition attempt was successfully posted on the bus.
 *  (5) No node was listening for an acquisition attempt so no node will
 *      reject it. This means we can (optionally) fast-forward to OPERATIONAL state.
 *  (6) Give the node which may already own this node ID time to react. If
 *      this delay is too short, the algorithm can't be used on non-real-time
 *      systems such as Linux. If it's long, nodes take a long time to reach
 *      OPERATIONAL state.
 *  (7) This message probes the bus load. Guard messages have higher priority
 *      than acquisition messages so once this message passes the RESTRAINED
 *      node can be sure that any potential guard message would have had a time
 *      to succeed.
 *  (8) This can happen either if another node already owns the tentative node
 *      ID or if a new node just joined the bus with the same tentative node ID
 *      while the earlier node was waiting for the acquisition attempt to pass.
 *  (9) If this rule is ignored there can be race conditions whereby the other
 *      competing RESTRAINED node unnessecerily also backs off.
 *  (10) This can happen if two previously disjoint networks with OPERATIONAL
 *      nodes are joined.
 *  (11) This considers a corner case of (10) where two OPERATIONAL nodes with
 *      identical Node IDs happen to send different data perfectly in sync.
 *  (12) We don't want two devices with the same firmware to follow the same
 *      node ID sequence.
 *  (13) If two nodes try to acquire the same node ID at the same time, they will
 *      notice this since their data payload is different and will therefore
 *      collide.
 *  (14) Guard messages must win arbitration against acquisition messages for (7)
 *      to work.
 *  (15) Frames with 11-bit Standard IDs are ignored because the address space
 *      is small and might be crowded with third-party nodes which don't can't
 *      defend their node ID, should a Fibre node try to acquire it.
 *      The drawback is that all Fibre messages lose arbitration against all
 *      standard frames (e.g. CANopen messages) and they are slightly less
 *      efficient.
 * 
 * TODO: specifiy how this works on FD vs non-FD
 */
struct CanAdapter final : FrameStreamSink {
    CanAdapter(TimerProvider* timer_provider, Domain* domain, CanInterface* intf, const char* intf_name) : timer_provider_(timer_provider), domain_(domain), intf_(intf), intf_name_(intf_name) {}

    void start(int tx_slots_begin, int tx_slots_end);
    void stop();

private:
    struct Mailbox {
        TxTask task;
        CBufIt end;
    };

    // FrameStreamSink implementation
    bool open_output_slot(uintptr_t* p_slot_id, Node* dest) final;
    bool close_output_slot(uintptr_t slot_id) final;
    bool start_write(TxTaskChain tasks) final;
    void cancel_write() final;

    can_Message_t get_heartbeat_message(bool dominant);
    void send_acquisition_message_0();
    void on_acquisition_msg_sent_0(bool success);
    void send_acquisition_message_1();
    void on_acquisition_msg_sent_1(bool success);
    void send_heartbeat();
    void on_heartbeat_sent(bool success);
    void on_timer();

    void on_can_msg(const can_Message_t& msg);
    void on_can_msg_sent(bool success);

    bool send_now(Mailbox* mailbox);

    TimerProvider* timer_provider_;
    Domain* domain_;
    CanInterface* intf_;
    const char* intf_name_;

    Timer* timer_;
    CanInterface::CanSubscription* heartbeat_subscription;

    int tx_slots_begin_;
    int tx_slots_end_;

    MiniRng rng;
    uint8_t node_id_;
    bool sending_heartbeat_ = false;
    enum {
        kJoining0,
        kJoining1,
        kOperational,
    } state_ = kJoining0;

    Mailbox* active_mailbox_ = nullptr;

    // Associates CAN IDs with Fibre nodes
    Map<uint8_t, Node*, 128> routes_ = {};


    // Number of messages that the backend can send simultaneously. On some
    // backends all simultaneous messages compete based on their arbitration
    // field. On other backends the messages are sent sequentially.
    Pool<Mailbox, 12> mailboxes = {};


    struct RxSlot {
        uint8_t can_id;
        uint8_t slot_id;
        ReceiverState state;

        bool operator==(const RxSlot& lhs) { return can_id == lhs.can_id && slot_id == lhs.slot_id; }
    };

    Map<RxSlot, CallContext, 128 * 3> rx_slots;

    // If this is too large, thrashing can occur at the destination
    static constexpr size_t kMaxOutputSlotsPerDest = 8;

    struct TxContext {
        Node* dest;
        uint8_t dest_pos = 0;
        uint8_t slot_id;

        SenderState state{};
    };

    Pool<TxContext, 128 * 3> tx_slots;
};

}


#endif // __FIBRE_CAN_BACKEND_HPP
