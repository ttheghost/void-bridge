(async () => {
    const { instance } = await WebAssembly.instantiateStreaming(fetch("main.wasm"));
    console.log(instance.exports.memory); // So we can access the memory view UI
    console.log(instance.exports.alloc(10));
    console.log(instance.exports.alloc(20));
    console.log(instance.exports.alloc(10));
})();