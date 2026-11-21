#pragma once

#include <QColor>
#include <QImage>
#include <QString>

// Renders a LaTeX math formula (the text between the $ signs, without them)
// with MicroTeX. Must be called from the GUI thread.
//
// `pixelSize` is the size of the surrounding text, `maxWidth` wraps long
// formulas and `dpr` is the device pixel ratio the image is drawn at (the
// image has it set). Returns a null image if the formula cannot be parsed, so
// the caller can show the source text instead. Results are cached.
QImage
renderMath(const QString &latex, int pixelSize, const QColor &color,
           int maxWidth, qreal dpr);
