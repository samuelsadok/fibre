#ifndef __FIBRE_REMOTE_NODE_HPP
#define __FIBRE_REMOTE_NODE_HPP

#ifndef __FIBRE_HPP
#error "This file should not be included directly. Include fibre.hpp instead."
#endif

namespace fibre {

class RemoteNode {
public:
    RemoteNode(Uuid uuid) : uuid_(uuid) {}

    Uuid get_uuid() const { return uuid_; }

    InputPipe* get_input_pipe(size_t id, bool* is_new);
    OutputPipe* get_output_pipe(size_t id);
    
    void add_output_channel(OutputChannel* channel);
    void remove_output_channel(OutputChannel* channel);
    
    void notify_output_pipe_ready();
    void notify_output_channel_ready();

    void schedule();

private:
    std::unordered_map<size_t, InputPipe> client_input_pipes_;
    std::unordered_map<size_t, InputPipe> server_input_pipes_;
    std::unordered_map<size_t, OutputPipe> client_output_pipes_;
    std::unordered_map<size_t, OutputPipe> server_output_pipes_;
    std::vector<OutputChannel*> output_channels_;
    Uuid uuid_;

#if CONFIG_SCHEDULER_MODE == SCHEDULER_MODE_PER_NODE_THREAD
    AutoResetEvent output_pipe_ready_;
    AutoResetEvent output_channel_ready_;
    
    void scheduler_loop() {
        for (;;) {
            output_pipes_ready_.wait();
            output_channels_ready_.wait();
            schedule();
        }
    }
#endif
};

}

#endif // __FIBRE_REMOTE_NODE_HPP
