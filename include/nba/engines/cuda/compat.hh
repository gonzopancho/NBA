#ifndef __NBA_ENGINES_CUDA_COMPAT_HH__
#define __NBA_ENGINES_CUDA_COMPAT_HH__

/*
 * This header is included by .cu sources.
 * We put only relevant data structures here for use in CUDA codes.
 * Note that the nvcc should support C++11 (CUDA v6.5 or higher).
 */

#include <nba/framework/config.hh>

struct datablock_kernel_arg {
    uint32_t total_item_count_in;
    uint32_t total_item_count_out;
    void *buffer_bases_in[NBA_MAX_COPROC_PPDEPTH];
    void *buffer_bases_out[NBA_MAX_COPROC_PPDEPTH];
    uint32_t item_count_in[NBA_MAX_COPROC_PPDEPTH];
    uint32_t item_count_out[NBA_MAX_COPROC_PPDEPTH];
    uint16_t item_size_in;
    uint16_t *item_sizes_in[NBA_MAX_COPROC_PPDEPTH];
    uint16_t item_size_out;
    uint16_t *item_sizes_out[NBA_MAX_COPROC_PPDEPTH];
    uint16_t *item_offsets_in[NBA_MAX_COPROC_PPDEPTH];
    uint16_t *item_offsets_out[NBA_MAX_COPROC_PPDEPTH];
};

#endif
