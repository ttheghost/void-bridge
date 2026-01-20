#include "mem.h"

#define OK 0
#define FAIL_BASIC_ALLOC 1
#define FAIL_DATA_CORRUPT 2
#define FAIL_REUSE 3
#define FAIL_COALESCE 4
#define FAIL_CALLOC 5
#define FAIL_REALLOC_SHRINK 6
#define FAIL_REALLOC_GROW 7
#define FAIL_GROW_HEAP 8

int check_pattern(uint8_t* p, usize_t size, uint8_t pattern) {
    for(usize_t i=0; i<size; i++) if (p[i] != pattern) return 0;
    return 1;
}

int test_heap_impl() {
    if (!vb_initialized) init_heap();

    void* p1 = malloc(100);
    if (!p1) return FAIL_BASIC_ALLOC;
    if (check_pattern((uint8_t*)p1, 100, 0xAA)) return FAIL_DATA_CORRUPT;

    // If we free p1, the next malloc(100) must return p1's address.
    free(p1);
    void* p2 = malloc(100);
    if (p1 != p2) return FAIL_REUSE;

    free(p2);

    uint8_t* zero_block = (uint8_t*)calloc(1, 50);
    if (!zero_block) return FAIL_BASIC_ALLOC;
    if (!check_pattern(zero_block, 50, 0x00)) return FAIL_CALLOC;
    free(zero_block);

    // [A] [B] [C]
    void* a = malloc(64);
    void* b = malloc(64);
    void* c = malloc(64);

    // [A] [Free] [C]
    free(b);
    // [  Free  ] [C]
    free(a);
    // [      Free      ]
    free(c);

    // If merged correctly, we have one 192 byte block starting at 'a'
    void* big = malloc(64 * 3);
    if (big != a) return FAIL_COALESCE;
    free(big);

    uint8_t* r1 = (uint8_t*)malloc(1000);
    for(int i=0; i<100; i++) r1[i] = (uint8_t)i;

    uint8_t* r2 = (uint8_t*)realloc(r1, 100);

    if (r1 != r2) return FAIL_REALLOC_SHRINK; // Should not have moved
    for(int i=0; i<100; i++) if (r2[i] != (uint8_t)i) return FAIL_DATA_CORRUPT;

    free(r2);

    // [A] [Free Neighbor] -> [  A (Expanded)  ]
    void* small = malloc(64);
    void* neighbor = malloc(64);
    free(neighbor);

    void* grown = realloc(small, 120); // Should eat 'neighbor'

    if (grown != small) return FAIL_REALLOC_GROW;
    free(grown);

    // Alloc something HUGE (e.g., 20 Pages) to force wasm_memory_grow
    usize_t huge_sz = 20 * 64 * 1024; 
    void* huge = malloc(huge_sz);
    if (!huge) return FAIL_GROW_HEAP;

    uint8_t* huge_ptr = (uint8_t*)huge;
    huge_ptr[huge_sz - 1] = 0xFE;
    if (huge_ptr[huge_sz - 1] != 0xFE) return FAIL_DATA_CORRUPT;

    free(huge);

    return OK;
}

extern "C" {

    uint32_t alloc(uint32_t size) {
        return (uint32_t)malloc(size);
    }
    
    uint32_t realloc(uint32_t ptr, uint32_t size) {
        return (uint32_t)realloc((void*)ptr, size);
    }

    void free(uint32_t ptr) {
        free((void*)ptr);
    }

    int test_heap() {
        return test_heap_impl();
    }

    heap_report_t* get_heap_report() {
        return intern_get_heap_report();
    }
}