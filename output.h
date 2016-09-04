#pragma once

#include <pulse/pulseaudio.h>

struct pulse {
  pa_threaded_mainloop *mainloop;
  pa_context *context;
  pa_stream *stream;
};

pa_usec_t latency_to_usec(int samplerate, int latency);
void output_init(struct pulse *pulse);
void create_stream(struct pulse *pulse, pa_sample_spec *ss);
void connect_stream(struct pulse *pulse, const pa_buffer_attr *bufattr);
void trigger_stream(struct pulse *pulse, pa_usec_t delay);
pa_usec_t get_latency(struct pulse *pulse);
void drain(void);
