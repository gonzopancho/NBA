#ifndef __NBA_INTRINSIC_HH__
#define __NBA_INTRINSIC_HH__

#include <rte_memory.h>
#ifdef RTE_CACHE_LINE_SIZE
#  define CACHE_LINE_SIZE RTE_CACHE_LINE_SIZE
#else
#  define CACHE_LINE_SIZE 64
#endif
#define __cache_aligned __attribute__((__aligned__(CACHE_LINE_SIZE)))

#define ALIGN_CEIL(x,a) (((x)+(a)-1)&~((a)-1))

namespace nba {

template<typename T>
static inline T bitselect(int cond, T trueval, T falseval)
{
    int cmp[2] = {0, -1};
    return ((trueval) & cmp[cond]) | ((falseval) & ~cmp[cond]);
}

static inline void memfence(void)
{
    asm volatile ("lfence" ::: "memory" );
}

static inline void mbarrier(void)
{
    asm volatile("": : :"memory");
}

static inline uint64_t swap64(uint64_t val)
{
    __asm__ ("bswap  %0" : "+r" (val));
    return val;
}

static inline void _cpuid(int i, uint32_t regs[4])
{
#ifdef _WIN32
    __cpuid((int *)regs, (int)i);
#else
    asm volatile
    ("cpuid" : "=a" (regs[0]), "=b" (regs[1]),
           "=c" (regs[2]), "=d" (regs[3])
     : "a" (i), "c" (0));
    // ECX is set to zero for CPUID function 4
#endif
}

static inline void dummy_cpuid()
{
    int i;
    uint32_t regs[4];
    _cpuid(i, regs);
}

/* Intel's documentation suggests use cpuid+rdtsc before meaurements and
 * rdtscp after measurements.  In both cases, we need to add CPUID
 * instruction to prevent out-of-order execution.
 * http://download.intel.com/embedded/software/IA/324264.pdf
 */
static inline uint64_t rdtsc(void)
{
    uint32_t low, high;
    asm volatile("rdtsc" : "=a" (low), "=d" (high));
    return ((uint64_t)low) | (((uint64_t)high) << 32);
}

/* rdtscp is an improved version of rdtsc.
 * It is almost same to memfence() + rdtsc() integrated into a single
 * instruction, with slightly less overhead.
 */
static inline uint64_t rdtscp(void)
{
    uint32_t low, high;
    uint32_t aux;
    asm volatile ("rdtscp" : "=a" (low), "=d" (high), "=c" (aux));
    return ((uint64_t)low | ((uint64_t)high << 32));
}

}

#endif
