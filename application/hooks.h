#ifndef __MCP_HOOKS_H__
#define __MCP_HOOKS_H__

#include <stdint.h>
#include <stdio.h>

//Avoid optimizing compilers moving code around this barrier
#define COMPILER_BARRIER() { __asm__ __volatile__("" ::: "memory");}

#define MAGIC_OP_ROI_BEGIN       (1030)

static inline void magic_op_1(uint64_t op) {
    COMPILER_BARRIER();
    __asm__ __volatile__("xchg %%ecx, %%ecx;" : : "c"(op));
    COMPILER_BARRIER();
}

static inline void roi_begin() {
    printf("ROI Begin in Execution.\n");
    magic_op_1(MAGIC_OP_ROI_BEGIN);
}


#endif /*__MCP_HOOKS_H__*/