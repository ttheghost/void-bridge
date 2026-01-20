#pragma once


#define ALIGNMENT 16
#define ALIGN_MASK (ALIGNMENT - 1)
#define NUM_BINS 32
#define MIN_BLOCK 32
#define INUSE 1
#define PREV_INUSE 2
#define FLAG_MASK 0xF

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long usize_t;
#define PAGE_SIZE 65536

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

typedef struct {
    usize_t total_heap_size;
    usize_t used_bytes;
    usize_t free_bytes;
    usize_t total_blocks;
    usize_t free_blocks;
    usize_t largest_free_block;
    usize_t fragmentation_percent; // 0 to 100
} heap_report_t;

extern uint8_t __heap_base;
static usize_t heap_end;
// For this test I am using Clang so I won't implement "wasm_memory_size" until the mem can do fondamentale task
#define wasm_mem_size() __builtin_wasm_memory_size(0)
#define wasm_mem_grow(bytes) __builtin_wasm_memory_grow(0, bytes)
static int vb_initialized = 0;
static heap_free_node_t* bins[NUM_BINS];
static heap_report_t report;

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
    return h->size_flags & ~(usize_t)FLAG_MASK;
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
                heap_header_t* n = next_block(h);
                if ((usize_t)n < heap_end) {
                    n->size_flags |= PREV_INUSE;
                }
                return h;
            }
            n = n->next;
        }
    }
    return 0;
}

static void split(heap_header_t* h, usize_t want) {
    usize_t total_size = block_size(h);
    int prev_status = is_prev_inuse(h);

    if (total_size >= want + MIN_BLOCK) {
        usize_t remain = total_size - want;
        set_header(h, want, 1, is_prev_inuse(h));

        heap_header_t* r = (heap_header_t*)((uint8_t*)h + want);
        set_header(r, remain, 0, 1);
    
        heap_footer_t* f = (heap_footer_t*)((uint8_t*)r + remain - sizeof(heap_footer_t));
        f->size = remain;

        bin_insert(bin_index(remain), (heap_free_node_t*)((uint8_t*)r + sizeof(heap_header_t)));
    } else {
        set_header(h, total_size, 1, prev_status);

        heap_header_t* n = next_block(h);

        if ((usize_t)n < heap_end) {
            n->size_flags |= PREV_INUSE; 
        }
    }
}

// Init

void init_heap() {
    usize_t heap_start = (usize_t)&__heap_base;
    usize_t current_pages = wasm_mem_size();
    heap_end = current_pages * PAGE_SIZE;

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

static heap_header_t* grow_heap(usize_t want) {
    // we assume want include the size of heap_header_t
    usize_t req_size = want + MIN_BLOCK;

    usize_t pages_needed = (req_size + PAGE_SIZE - 1) / PAGE_SIZE;

    usize_t old_pages = wasm_mem_grow(pages_needed);
    if (old_pages == (usize_t)-1) return 0;

    usize_t start_addr = old_pages * PAGE_SIZE;
    usize_t added_size = pages_needed * PAGE_SIZE;
    
    heap_end += added_size;

    heap_header_t* h = (heap_header_t*)start_addr;

    heap_header_t* prev = prev_block(h);
    if (prev && !is_inuse(prev))
    {
        usize_t prev_size = block_size(prev);
        heap_free_node_t* pn = (heap_free_node_t*)((uint8_t*) prev + sizeof(heap_header_t));
        bin_remove(bin_index(prev_size), pn);
        added_size += prev_size;
        h = prev;
    }
    
    set_header(h, added_size, 0, is_prev_inuse(h));

    heap_footer_t* f = (heap_footer_t*)((uint8_t*)h + added_size - sizeof(heap_footer_t));
    f->size = added_size;

    bin_insert(bin_index(added_size), (heap_free_node_t*)((uint8_t*)h + sizeof(heap_header_t)));

    return h;
}

// malloc

void* malloc(usize_t size) {
    if (!size) return 0;

    if (!vb_initialized) {
        init_heap();
    }

    usize_t want = align_up(size + sizeof(heap_header_t));
    if (want < MIN_BLOCK) want = MIN_BLOCK;
    heap_header_t* h = find_block(want);
    if (!h)
    {
        if (!grow_heap(want)) return 0;

        h = find_block(want);
        if (!h) return 0;
    }
    
    split(h, want);
    return (uint8_t*)h + sizeof(heap_header_t);
}

void* calloc(usize_t n, usize_t size) {
    usize_t total = n * size;
    if (n && total / n != size) return 0;
    
    void* p = malloc(total);
    if(!p) return 0;
    
    // We have 16 byte alignment, so we can optimize zeroing memory
    usize_t* pu = (usize_t*)p;
    
    usize_t i = 0;
    for (i = 0; i < total / sizeof(usize_t); i++) pu[i] = 0;

    i = i * sizeof(usize_t);
    for (; i < total; i++) {
        ((uint8_t*)p)[i] = 0;
    }

    return p;
}

void free(void* p) {
    if (!p) return;

    heap_header_t* h = (heap_header_t*)((uint8_t*)p - sizeof(heap_header_t));
    usize_t size = block_size(h);

    heap_header_t* next = next_block(h);
    if ((usize_t)next < heap_end && !is_inuse(next))
    {
        usize_t next_size = block_size(next);
        heap_free_node_t* nn = (heap_free_node_t*)((uint8_t*)next + sizeof(heap_header_t));
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

void* realloc(void *p, usize_t n) {
    if (!p) return malloc(n);

    if (n == 0)
    {
        free(p);
        return 0;
    }
    
    heap_header_t* h = (heap_header_t*)((uint8_t*)p - sizeof(heap_header_t));
    usize_t current_total_size = block_size(h);
    usize_t want_total = align_up(n + sizeof(heap_header_t));

    if (want_total < MIN_BLOCK) want_total = MIN_BLOCK;
    

    // Shrinking
    if (current_total_size >= want_total + MIN_BLOCK)
    {
        usize_t remain = current_total_size - want_total;

        set_header(h, want_total, 1, is_prev_inuse(h));

        heap_header_t* r = (heap_header_t*)((uint8_t*)h + want_total);

        // We will use free to merge it with the next next if fusable
        // so we will mark it temporarily as in_use so free accept it safely
        set_header(r, remain, 1, 1);

        // free will merge, set the footer and add to bin
        free((uint8_t*)r + sizeof(heap_header_t));

        return p;
    }

    // Exact fit or tiny change (less than MIN_BLOCK)
    if (current_total_size >= want_total)
    {
        return p;
    }

    // Growing
    heap_header_t* next = next_block(h);

    if ((usize_t)next < heap_end && !is_inuse(next))
    {
        usize_t next_size = block_size(next);
        if (current_total_size + next_size >= want_total)
        {
            heap_free_node_t* nn = (heap_free_node_t*) ((uint8_t*)next + sizeof(heap_header_t));
            bin_remove(bin_index(next_size), nn);

            usize_t new_total = next_size + current_total_size;
            set_header(h, new_total, 1, is_prev_inuse(h));

            heap_header_t* next_next = next_block(h);
            if ((usize_t)next_next < heap_end) {
                next_next->size_flags |= PREV_INUSE;
            }

            if (new_total >= want_total + MIN_BLOCK)
            {
                split(h, want_total);
            }

            return p;
        }
    }

    // malloc - copy - free
    void* new_p = malloc(n);
    if (!new_p) return 0;
    
    usize_t* s = (usize_t*)p;
    usize_t* d = (usize_t*)new_p;
    usize_t current_payload = (current_total_size - sizeof(heap_header_t));
    usize_t copy_len = (n < current_payload) ? n : current_payload;

    usize_t words = copy_len / sizeof(usize_t);
    for (usize_t i = 0; i < words; i++) d[i] = s[i];

    uint8_t* s8 = (uint8_t*)p;
    uint8_t* d8 = (uint8_t*)new_p;
    for(usize_t i = words * sizeof(usize_t); i < copy_len; i++) d8[i] = s8[i];

    free(p);

    return new_p;
}

heap_report_t* intern_get_heap_report() {
    if (!vb_initialized) init_heap();
    
    report.total_heap_size = heap_end;
    report.used_bytes = 0;
    report.free_bytes = 0;
    report.total_blocks = 0;
    report.free_blocks = 0;
    report.largest_free_block = 0;

    heap_header_t* h = (heap_header_t*)align_up((usize_t)&__heap_base);

    while ((usize_t)h < heap_end) {
        usize_t size = block_size(h);
        if (size == 0) break; // Corrupt heap protection

        report.total_blocks++;
        if (is_inuse(h)) {
            report.used_bytes += size;
        } else {
            report.free_bytes += size;
            report.free_blocks++;
            if (size > report.largest_free_block) report.largest_free_block = size;
        }
        h = next_block(h);
    }
    
    report.fragmentation_percent = report.free_bytes > 0 
        ? 100 - ((report.largest_free_block * 100) / report.free_bytes) 
        : 0;

    return &report;
}