// Checks that include/PixmapPost.hpp (tint + invert + high contrast in one
// pass) gives byte-for-byte what MuPDF's own passes give, one after another.
//
//   c++ -std=c++20 -O2 -Iinclude -Ithirdparty/mupdf/include \
//       tests/pixmap_post_test.cpp \
//       thirdparty/mupdf/build/release/libmupdf.a \
//       thirdparty/mupdf/build/release/libmupdf-third.a -lm -lpthread \
//       -o /tmp/pixmap_post_test && /tmp/pixmap_post_test

#include "PixmapPost.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

static void
highContrastLut(unsigned char lut[256], int black, int white)
{
    // The same table the renderer builds (see buildHighContrastLUT).
    if (black < 0)
        black = 0;
    if (white > 255)
        white = 255;
    if (white <= black)
    {
        const int cut = (black + white) / 2;
        for (int i = 0; i < 256; ++i)
            lut[i] = (i <= cut) ? 0 : 255;
        return;
    }
    const int span = white - black;
    for (int i = 0; i < 256; ++i)
    {
        int v;
        if (i <= black)
            v = 0;
        else if (i >= white)
            v = 255;
        else
            v = ((i - black) * 255 + span / 2) / span;
        lut[i] = static_cast<unsigned char>(v);
    }
}

int
main()
{
    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx)
        return 2;

    struct Space
    {
        const char *name;
        fz_colorspace *cs;
    } spaces[] = {{"gray", fz_device_gray(ctx)},
                  {"rgb", fz_device_rgb(ctx)},
                  {"bgr", fz_device_bgr(ctx)}};

    const int colors[][2] = {{0x000000, 0xFFFFFF}, {0x1e1e2e, 0xcdd6f4},
                             {0xFF0000, 0x0000FF}, {0x336699, 0xFFEECC},
                             {0xFFFFFF, 0x000000}, {0x102030, 0x102030}};
    const int hcPoints[][2]  = {{0, 255}, {30, 220}, {100, 120}, {128, 128},
                                {200, 50}};

    std::mt19937 rng(12345);
    int checked = 0, failed = 0;

    for (const Space &sp : spaces)
        for (int w : {1, 7, 64, 101})
            for (const auto &col : colors)
                for (int tint = 0; tint < 2; ++tint)
                    for (int invert = 0; invert < 2; ++invert)
                        for (int hcOn = 0; hcOn < 2; ++hcOn)
                            for (const auto &hcp : hcPoints)
                            {
                                if (!hcOn && &hcp != &hcPoints[0])
                                    continue;
                                const int h = 9;
                                fz_pixmap *a = fz_new_pixmap(
                                    ctx, sp.cs, w, h, nullptr, 0);
                                fz_pixmap *b = fz_clone_pixmap(ctx, a);
                                const size_t bytes = size_t(a->stride) * h;
                                for (size_t i = 0; i < bytes; ++i)
                                    a->samples[i] = rng() & 255;
                                std::memcpy(b->samples, a->samples, bytes);

                                unsigned char hc[256];
                                highContrastLut(hc, hcp[0], hcp[1]);

                                // reference: MuPDF's passes one by one
                                if (tint)
                                    fz_tint_pixmap(ctx, a, col[0], col[1]);
                                if (invert)
                                    fz_invert_pixmap(ctx, a);
                                if (hcOn)
                                    for (int y = 0; y < h; ++y)
                                        for (int i = 0; i < w * a->n; ++i)
                                            a->samples[y * a->stride + i]
                                                = hc[a->samples[y * a->stride
                                                                + i]];

                                // fused
                                unsigned char lut[3][256];
                                bool ok = pixmap_post::buildLuts(
                                    ctx, b, tint, col[0], col[1], invert,
                                    hcOn ? hc : nullptr, lut);
                                if (ok)
                                    pixmap_post::apply(b, lut);

                                bool same = ok;
                                for (int y = 0; same && y < h; ++y)
                                    same = std::memcmp(
                                               a->samples + y * a->stride,
                                               b->samples + y * b->stride,
                                               size_t(w) * a->n)
                                           == 0;
                                ++checked;
                                if (!same)
                                {
                                    ++failed;
                                    if (failed <= 10)
                                        std::printf(
                                            "MISMATCH %s w=%d tint=%d "
                                            "(%06x,%06x) invert=%d hc=%d "
                                            "(%d,%d)\n",
                                            sp.name, w, tint, col[0], col[1],
                                            invert, hcOn, hcp[0], hcp[1]);
                                }
                                fz_drop_pixmap(ctx, a);
                                fz_drop_pixmap(ctx, b);
                            }

    std::printf("%d combinations checked, %d mismatches\n", checked, failed);
    fz_drop_context(ctx);
    return failed ? 1 : 0;
}
