#pragma once

// Page colour post-processing done in one pass.
//
// A render can be tinted (black and white mapped to the page colours),
// inverted and tone-stretched (high contrast). Each is a per-byte mapping of a
// colour channel, so the three compose into one 256-entry table per channel
// and one walk over the pixels does all of them.
//
// Same arithmetic as MuPDF's fz_tint_pixmap and fz_invert_pixmap, applied in
// the same order: tint, then invert, then high contrast.

#include <utility>

extern "C"
{
#include <mupdf/fitz.h>
}

namespace pixmap_post
{
// Number of colour channels handled (1 for gray, 3 for RGB/BGR); 0 if this
// pixmap is not one the fused pass handles (alpha, spot colours, CMYK...).
inline int
channels(fz_context *ctx, const fz_pixmap *pix) noexcept
{
    if (!pix || !pix->colorspace || pix->alpha || pix->s)
        return 0;
    switch (fz_colorspace_type(ctx, pix->colorspace))
    {
        case FZ_COLORSPACE_GRAY:
            return pix->n == 1 ? 1 : 0;
        case FZ_COLORSPACE_RGB:
        case FZ_COLORSPACE_BGR:
            return pix->n == 3 ? 3 : 0;
        default:
            return 0;
    }
}

// `black` / `white`: 0xRRGGBB the page's black and white are mapped to (only
// used if `tint`). `highContrast`: a 256-entry table applied last, or null.
// Fills lut[c][v] for the channels() channels; false if not handled.
inline bool
buildLuts(fz_context *ctx, const fz_pixmap *pix, bool tint, int black,
          int white, bool invert, const unsigned char *highContrast,
          unsigned char lut[3][256]) noexcept
{
    const int n = channels(ctx, pix);
    if (n == 0)
        return false;

    int rb = (black >> 16) & 255, gb = (black >> 8) & 255, bb = black & 255;
    int rw = (white >> 16) & 255, gw = (white >> 8) & 255, bw = white & 255;

    int base[3] = {0, 0, 0}, mul[3] = {0, 0, 0};
    if (n == 1)
    {
        gw      = (rw + gw + bw) / 3;
        gb      = (rb + gb + bb) / 3;
        base[0] = gb;
        mul[0]  = gw - gb;
    }
    else
    {
        if (fz_colorspace_type(ctx, pix->colorspace) == FZ_COLORSPACE_BGR)
        {
            std::swap(rb, bb);
            std::swap(rw, bw);
        }
        base[0] = rb;
        mul[0]  = rw - rb;
        base[1] = gb;
        mul[1]  = gw - gb;
        base[2] = bb;
        mul[2]  = bw - bb;
    }

    for (int c = 0; c < n; ++c)
        for (int v = 0; v < 256; ++v)
        {
            int t = v;
            if (tint)
                t = static_cast<unsigned char>(base[c] + fz_mul255(v, mul[c]));
            if (invert)
                t = 255 - t;
            if (highContrast)
                t = highContrast[t];
            lut[c][v] = static_cast<unsigned char>(t);
        }
    return true;
}

// One pass over the pixels (the padding at the end of each row is left alone).
inline void
apply(const fz_pixmap *pix, const unsigned char lut[3][256]) noexcept
{
    unsigned char *row = pix->samples;
    if (pix->n == 1)
    {
        for (int y = 0; y < pix->h; ++y, row += pix->stride)
            for (int x = 0; x < pix->w; ++x)
                row[x] = lut[0][row[x]];
        return;
    }
    for (int y = 0; y < pix->h; ++y, row += pix->stride)
    {
        unsigned char *s = row;
        for (int x = 0; x < pix->w; ++x, s += 3)
        {
            s[0] = lut[0][s[0]];
            s[1] = lut[1][s[1]];
            s[2] = lut[2][s[2]];
        }
    }
}
} // namespace pixmap_post
