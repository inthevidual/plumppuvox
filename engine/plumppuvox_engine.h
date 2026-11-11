// C interface of the PlumppuVox engine (Rust static library, see engine/src/lib.rs).
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PvEngine PvEngine;

PvEngine* plumppuvox_create (uint32_t channels);
void plumppuvox_free (PvEngine* engine);
uint32_t plumppuvox_hop_size (const PvEngine* engine);
uint32_t plumppuvox_sample_rate (const PvEngine* engine);
uint32_t plumppuvox_latency (const PvEngine* engine);
void plumppuvox_reset (PvEngine* engine);
// Share of untouched input in the output: 1 = pass through, 0 = fully processed.
void plumppuvox_set_dry_mix (PvEngine* engine, float dry);
int32_t plumppuvox_process (PvEngine* engine, const float* input, float* output);

#ifdef __cplusplus
}
#endif
