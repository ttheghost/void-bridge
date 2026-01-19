(async () => {
    const instance = WebAssembly.instantiateStreaming(fetch("main.c"));
    console.log(instance);
})();