#pragma once


#define ALIGNMENT 16
#define ALIGN_MASK (ALIGNMENT - 1)
#define NUM_BINS 32
#define MIN_BLOCK 32
#define INUSE 1
#define PREV_INUSE 2

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long usize_t;

typedef struct
{
    usize_t size_flags;
} heap_header_t;

typedef struct
{
    usize_t size;
} heap_footer_t;

typedef struct free_node
{
    struct free_node* next;
    struct free_node* prev;
} heap_free_node_t;

extern uint8_t __heap_base;
static usize_t heap_end;
// For this test I am using Clang so I won't implement "wasm_memory_size" until the mem can do fondamentale task
#define wasm_mem_size() __builtin_wasm_memory_size(0)
static int vb_initialized = 0;
static heap_free_node_t* bins[NUM_BINS];

// DLinked list helper
static int bin_index(usize_t size) {
    int i = 0;
    size >>= 4;
    while (size && i < NUM_BINS - 1) {
        size >>= 1;
        i++;
    }
    return i;
}

static void bin_insert(int b, heap_free_node_t* n) {
    n->prev = 0;
    n->next = bins[b];
    if (bins[b]) bins[b]->prev = n;
    bins[b] = n;
}

static void bin_remove(int b, heap_free_node_t* n) {
    if (n->prev) n->prev->next = n->next;
    else bins[b] = n->next;
    if (n->next) n->next->prev = n->prev;
}

// Helpers

static usize_t align_up(usize_t size) {
    return (size + ALIGN_MASK) & ~ALIGN_MASK;
}

static usize_t block_size(heap_header_t* h) {
    return h->size_flags & ~(usize_t)0xF;
}

static void set_header(heap_header_t* h, usize_t size, int inuse, int prev) {
    h->size_flags = size | (inuse ? INUSE : 0) | (prev ? PREV_INUSE : 0);
}

static int is_inuse(heap_header_t* h) {
    return h->size_flags & INUSE;
}

static int is_prev_inuse(heap_header_t* h) {
    return h->size_flags & PREV_INUSE;
}

static heap_header_t* next_block(heap_header_t* h) {
    return (heap_header_t*)((uint8_t*)h + block_size(h));
}

static heap_header_t* prev_block(heap_header_t* h) {
    if (is_prev_inuse(h)) return 0;
    heap_footer_t* f = (heap_footer_t*) ((uint8_t*)h - sizeof(heap_footer_t));
    return (heap_header_t*)((uint8_t*)h - f->size);
}

static heap_header_t* find_block(usize_t want) {
    for (int b = bin_index(want); b < NUM_BINS; b++)
    {
        heap_free_node_t* n = bins[b];
        while (n)
        {
            heap_header_t* h = (heap_header_t*)((uint8_t*)n - sizeof(heap_header_t));
            if (block_size(h) >= want)
            {
                bin_remove(b, n);
                set_header(h, block_size(h), 1, is_prev_inuse(h));
                return h;
            }
            n = n->next;
        }
    }
    return 0;
}

static void split(heap_header_t* h, usize_t want) {
    usize_t size = block_size(h);
    if (size < want + MIN_BLOCK) return;

    usize_t remain = size - want;
    set_header(h, want, 1, is_prev_inuse(h));

    heap_header_t* r = (heap_header_t*)((uint8_t*)h + want);
    set_header(r, remain, 0, 1);
    
    heap_footer_t* f = (heap_footer_t*)((uint8_t*)r + remain - sizeof(heap_footer_t));
    f->size = remain;

    bin_insert(bin_index(remain), (heap_free_node_t*)((uint8_t*)r + sizeof(heap_header_t)));
}

// Init

void init_heap() {
    usize_t heap_start = (usize_t)&__heap_base;
    usize_t current_pages = wasm_mem_size();
    heap_end = current_pages * 64 * 1024; // WASM has usually 64KB per page

    usize_t start = align_up(heap_start);

    if (start + MIN_BLOCK + sizeof(heap_header_t) >= heap_end) {
        return;
    }

    usize_t size = heap_end - start;

    heap_header_t* h = (heap_header_t*)start;
    set_header(h, size, 0, 1);

    heap_footer_t* f = (heap_footer_t*)((uint8_t*)h + size - sizeof(heap_footer_t));
    f->size = size;

    bin_insert(bin_index(size), (heap_free_node_t*)((uint8_t*)h + sizeof(heap_header_t)));

    vb_initialized = 1;
}

// malloc

void* malloc(usize_t size) {
    if (!size) return 0;

    if (!vb_initialized) {
        init_heap();
    }

    usize_t want = align_up(size + sizeof(heap_header_t) + sizeof(heap_footer_t));
    if (want < MIN_BLOCK) want = MIN_BLOCK;
    heap_header_t* h = find_block(want);
    if (!h)
    {
        // Grow
        return 0;
    }
    
    split(h, want);
    return (uint8_t*)h + sizeof(heap_header_t);
}

void free(void* p) {
    if (!p) return;

    heap_header_t* h = (heap_header_t*)((uint8_t*)p - sizeof(heap_header_t));
    usize_t size = block_size(h);

    heap_header_t* next = next_block(h);
    if ((usize_t)next < heap_end && !is_inuse(next))
    {
        usize_t next_size = block_size(next);
        heap_free_node_t* nn = (heap_free_node_t*) ((uint8_t*)next + sizeof(heap_header_t));
        bin_remove(bin_index(next_size), nn);
        size += next_size;
    }

    heap_header_t* prev = prev_block(h);
    if (prev && !is_inuse(prev)) {
        usize_t prev_size = block_size(prev);
        heap_free_node_t* pn = (heap_free_node_t*)((uint8_t*)prev + sizeof(heap_header_t));
        bin_remove(bin_index(prev_size), pn);
        size += prev_size;
        h = prev;
    }

    set_header(h, size, 0, is_prev_inuse(h));

    heap_footer_t* f = (heap_footer_t*)((uint8_t*)h + size - sizeof(heap_footer_t));
    f->size = size;

    heap_header_t* final_next = next_block(h);
    if ((usize_t)final_next < heap_end) {
        final_next->size_flags &= ~PREV_INUSE;
    }

    bin_insert(bin_index(size), (heap_free_node_t*)((uint8_t*)h + sizeof(heap_header_t)));
}
