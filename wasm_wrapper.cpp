#include <cstdint>

#include <emscripten/emscripten.h>

#include "HalideBuffer.h"
#include "nebulastack_astro_pipeline.h"

extern "C" EMSCRIPTEN_KEEPALIVE int nebulastack_process_tile(
    std::uint16_t *input_data,
    std::uint8_t *output_data,
    int width,
    int height,
    float deconvolution,
    float deconvolution_radius,
    float wavelet_fine,
    float wavelet_medium,
    float wavelet_large) {
    if (input_data == nullptr || output_data == nullptr || width <= 0 || height <= 0) {
        return -1;
    }

    auto input = Halide::Runtime::Buffer<std::uint16_t>::make_interleaved(input_data, width, height, 3);
    auto output = Halide::Runtime::Buffer<std::uint8_t>::make_interleaved(output_data, width, height, 4);

    return nebulastack_astro_pipeline(input, deconvolution, deconvolution_radius, wavelet_fine,
                                      wavelet_medium, wavelet_large, output);
}
