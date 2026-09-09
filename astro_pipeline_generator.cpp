#include "Halide.h"

using namespace Halide;

class AstroPipeline : public Generator<AstroPipeline> {
public:
    Input<Buffer<uint16_t, 3>> input{"input"};
    Input<float> deconvolution{"deconvolution", 0.18f};
    Input<float> deconvolution_radius{"deconvolution_radius", 1.2f};
    Input<float> wavelet_fine{"wavelet_fine", 0.14f};
    Input<float> wavelet_medium{"wavelet_medium", 0.09f};
    Input<float> wavelet_large{"wavelet_large", 0.04f};
    Output<Buffer<uint8_t, 3>> output{"output"};

    Func gaussian_blur(Func source, Expr sigma, const std::string &name) {
        Func horizontal{name + "_horizontal"};
        Func vertical{name + "_vertical"};
        Expr safe_sigma = clamp(sigma, 0.6f, 2.8f);
        Expr denominator = 2.0f * safe_sigma * safe_sigma;
        Expr weight_one = exp(-1.0f / denominator);
        Expr weight_two = exp(-4.0f / denominator);
        Expr normalization = 1.0f + 2.0f * weight_one + 2.0f * weight_two;
        horizontal(x, y) =
            (source(x, y) + weight_one * (source(x - 1, y) + source(x + 1, y)) +
             weight_two * (source(x - 2, y) + source(x + 2, y))) /
            normalization;
        vertical(x, y) =
            (horizontal(x, y) + weight_one * (horizontal(x, y - 1) + horizontal(x, y + 1)) +
             weight_two * (horizontal(x, y - 2) + horizontal(x, y + 2))) /
            normalization;
        return vertical;
    }

    Func atrous_blur(Func source, int step, const std::string &name) {
        Func horizontal{name + "_horizontal"};
        Func vertical{name + "_vertical"};
        horizontal(x, y) =
            (source(x - 2 * step, y) + 4.0f * source(x - step, y) +
             6.0f * source(x, y) + 4.0f * source(x + step, y) + source(x + 2 * step, y)) /
            16.0f;
        vertical(x, y) =
            (horizontal(x, y - 2 * step) + 4.0f * horizontal(x, y - step) +
             6.0f * horizontal(x, y) + 4.0f * horizontal(x, y + step) + horizontal(x, y + 2 * step)) /
            16.0f;
        return vertical;
    }

    void generate() {
        Func bounded = BoundaryConditions::repeat_edge(input);
        Func linear{"linear"};
        Func luminance{"luminance"};
        linear(x, y, c) = cast<float>(bounded(x, y, c)) / 65535.0f;
        luminance(x, y) = linear(x, y, 0) * 0.2126f + linear(x, y, 1) * 0.7152f +
                          linear(x, y, 2) * 0.0722f;

        Func psf = gaussian_blur(luminance, deconvolution_radius, "psf");
        Func ratio{"ratio"};
        ratio(x, y) = luminance(x, y) / max(psf(x, y), 0.00001f);
        Func correction = gaussian_blur(ratio, deconvolution_radius, "correction");
        Func restored{"restored"};
        Expr restoration = luminance(x, y) * clamp(correction(x, y), 0.74f, 1.26f);
        Expr signal_normalized = clamp((luminance(x, y) - 0.002f) / 0.048f, 0.0f, 1.0f);
        Expr signal = signal_normalized * signal_normalized * (3.0f - 2.0f * signal_normalized);
        restored(x, y) = luminance(x, y) + (restoration - luminance(x, y)) *
                                               clamp(deconvolution, 0.0f, 1.0f) * 0.72f * signal;

        Func layer_one = atrous_blur(restored, 1, "layer_one");
        Func layer_two = atrous_blur(layer_one, 2, "layer_two");
        Func layer_three = atrous_blur(layer_two, 4, "layer_three");
        Func enhanced{"enhanced"};
        Expr fine_detail = restored(x, y) - layer_one(x, y);
        Expr medium_detail = layer_one(x, y) - layer_two(x, y);
        Expr large_detail = layer_two(x, y) - layer_three(x, y);
        enhanced(x, y) = clamp(
            restored(x, y) + fine_detail * smooth_detail_gate(abs(fine_detail), 0.0015f) *
                                  clamp(wavelet_fine, 0.0f, 1.0f) * 1.15f +
                medium_detail * smooth_detail_gate(abs(medium_detail), 0.0010f) *
                    clamp(wavelet_medium, 0.0f, 1.0f) * 1.05f +
                large_detail * smooth_detail_gate(abs(large_detail), 0.0008f) *
                    clamp(wavelet_large, 0.0f, 1.0f) * 0.90f,
            0.0f, 1.2f);

        Expr scale = clamp(enhanced(x, y) / max(luminance(x, y), 0.000002f), 0.28f, 3.2f);
        Expr protection = smooth_detail_gate(luminance(x, y), 0.004f);
        Expr safe_scale = 1.0f + (scale - 1.0f) * protection;
        Expr corrected = clamp(linear(x, y, clamp(c, 0, 2)) * safe_scale, 0.0f, 1.0f);
        Expr srgb = select(corrected <= 0.0031308f, corrected * 12.92f,
                           1.055f * pow(corrected, 1.0f / 2.4f) - 0.055f);
        Expr encoded = cast<uint8_t>(clamp(srgb * 255.0f + 0.5f, 0.0f, 255.0f));
        output(x, y, c) = select(c == 3, cast<uint8_t>(255), encoded);
    }

    void schedule() {
        input.dim(0).set_stride(3);
        input.dim(2).set_stride(1).set_bounds(0, 3);
        output.dim(0).set_stride(4);
        output.dim(2).set_stride(1).set_bounds(0, 4);

        Var xo{"xo"}, xi{"xi"};
        output.bound(c, 0, 4).reorder(c, x, y).unroll(c).split(x, xo, xi, 8).vectorize(xi);
        if (get_target().has_feature(Target::WasmThreads)) {
            output.parallel(y);
        }
    }

private:
    Expr smooth_detail_gate(Expr value, Expr threshold) {
        Expr normalized = clamp((value - threshold) / max(threshold * 6.0f, 0.000001f), 0.0f, 1.0f);
        return normalized * normalized * (3.0f - 2.0f * normalized);
    }

    Var x{"x"}, y{"y"}, c{"c"};
};

HALIDE_REGISTER_GENERATOR(AstroPipeline, nebulastack_astro_pipeline)
