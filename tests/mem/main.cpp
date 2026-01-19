#include "mem.h"

#define E_OK 0
#define E_ALLOC_FAIL 1
#define E_DATA_CORRUPT 2
#define E_MERGE_FAIL 3
#define E_POINTER_MISMATCH 4

int verify_pattern(uint8_t* ptr, usize_t size, uint8_t pattern) {
    for (usize_t i = 0; i < size; i++) {
        if (ptr[i] != pattern) return 0;
    }
    return 1;
}

int test_heap_impl() {
    if (!vb_initialized) init_heap();

    uint8_t* p1 = (uint8_t*)malloc(100);
    if (!p1) return E_ALLOC_FAIL;

    for (int i = 0; i < 100; i++) p1[i] = 0x41;
    if (!verify_pattern(p1, 100, 0x41)) return E_DATA_CORRUPT;

    // [ P2 ] [ P3 ] [ P4 ]
    uint8_t* p2 = (uint8_t*)malloc(64);
    uint8_t* p3 = (uint8_t*)malloc(64);
    uint8_t* p4 = (uint8_t*)malloc(64);

    if (!p2 || !p3 || !p4) return E_ALLOC_FAIL;

    // Heap should be: [P2:InUse] [Free:64] [P4:InUse]
    free(p3);
    // Heap should be: [Free:128 (Merged P2+P3)] [P4:InUse]
    free(p2);
    // Heap should be: [Free:192 (Merged P2+P3+P4)]
    free(p4);

    uint8_t* check = (uint8_t*)malloc(64 * 3);

    if (check != p2) return E_MERGE_FAIL;

    free(check);

    void* last = malloc(MIN_BLOCK);
    free(last);

    return E_OK;
}

extern "C" {

    uint32_t alloc(uint32_t size) {
        return (uint32_t)malloc(size);
    }

    void free(uint32_t ptr) {
        free((void*)ptr);
    }

    int test_heap() {
        return test_heap_impl();
    }
}