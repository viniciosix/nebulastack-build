# NebulaStack Halide / WebAssembly

Este diretório contém o pipeline AOT usado no editor astronômico do NebulaStack.

O gerador Halide processa blocos RGB lineares de 16 bits e produz RGBA sRGB. Ele aplica deconvolução regularizada e três escalas de wavelets à trous com proteção de ruído. O navegador envia blocos pequenos para o módulo, mantendo o pico de memória previsível em celulares. Se WebAssembly SIMD não estiver disponível, o Worker conserva a implementação JavaScript atual.

## Build reproduzível

- Halide 21.0.0, validado por SHA-256.
- Alvo `wasm-32-wasmrt-wasm_simd128`.
- Emscripten 6.0.9 no workflow do repositório de build.
- Sem threads, para não depender de isolamento COOP/COEP no PWA público.

Com o Emscripten ativado, execute:

```bash
chmod +x build-wasm.sh
./build-wasm.sh
```

Os arquivos `astro-halide.js`, `astro-halide.wasm` e `SHA256SUMS` serão gravados em `dist/`.
