function logHeapStatus(instance, label) {
    const ptr = instance.exports.get_heap_report();
    const mem = new Uint32Array(instance.exports.memory.buffer);
    const offset = ptr / 4; // Convert byte pointer to array index

    const report = {
        TotalSize: (mem[offset + 0] / 1024).toFixed(1) + ' KB',
        Used: mem[offset + 1],
        Free: mem[offset + 2],
        TotalBlocks: mem[offset + 3],
        FreeBlocks: mem[offset + 4],
        LargestBlock: mem[offset + 5],
        FragPercent: mem[offset + 6] + '%'
    };

    console.table({ [label]: report });
    
    if (report.fragPercent > 50) {
        console.warn("High Fragmentation! Consider restarting Wasm or allocating simpler patterns.");
    }
}

(async () => {
    const { instance } = await WebAssembly.instantiateStreaming(fetch("main.wasm"));
    window.instance = instance;
    console.log(instance.exports.memory); // So we can access the memory view UI

    console.log("--- Running Allocator Tests ---");
    const exports = instance.exports;
    const memory = exports.memory;

    logHeapStatus(instance, "Initial State");

    const result = exports.test_heap();

    logHeapStatus(instance, "Final State");

    const errors = [
        "OK",
        "FAIL: Basic Malloc returned NULL",
        "FAIL: Data Corruption (Read != Write)",
        "FAIL: Bin Reuse (Alloc -> Free -> Alloc != Same Addr)",
        "FAIL: Coalescing (Sandwich test failed)",
        "FAIL: Calloc (Memory was dirty)",
        "FAIL: Realloc Shrink (Moved pointer unnecessarily)",
        "FAIL: Realloc Grow (Moved pointer instead of merging)",
        "FAIL: Grow Heap (Could not expand Wasm Memory)"
    ];

    if (result === 0) {
        console.log("PASSED: All checks green.");
    } else {
        console.error("FAILED:", errors[result] || "Unknown Error");
    }
})();