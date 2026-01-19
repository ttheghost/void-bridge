#include "mem.h"

extern "C" {

    uint32_t alloc(uint32_t size) {
        return (uint32_t)malloc(size);
    }

}