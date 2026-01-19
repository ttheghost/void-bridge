(async () => {
    const { instance } = await WebAssembly.instantiateStreaming(fetch("main.wasm"));
    console.log(instance.exports.memory); // So we can access the memory view UI
    let p = instance.exports.alloc(600);
    console.log(p);
    console.log(instance.exports.alloc(63500));
    instance.exports.free(p);
    console.log(instance.exports.alloc(600));
    console.log(instance.exports.alloc(220));
})();