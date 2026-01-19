(async () => {
    const { instance } = await WebAssembly.instantiateStreaming(fetch("main.wasm"));
    console.log(instance.exports.memory); // So we can access the memory view UI

    console.log("--- Running Allocator Tests ---");
    const result = instance.exports.test_heap();

    if (result === 0) {
        console.log("SUCCESS: All heap sanity checks passed.");
    } else {
        console.error("FAILED: Error Code", result);
        
        if (result === 3) console.log("   -> Merging failed (Fragmentation detected)");
        if (result === 2) console.log("   -> Data corruption (Memory overlap?)");
    }
})();