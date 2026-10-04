#pragma once

#include <QImage>
#include <QString>

// Prepares an image to be sent to a vision model inside a chat message:
// scaled down so its longest side is at most `maxSide` pixels, and encoded as
// a `data:` URL. PNG keeps text and diagrams crisp; if that comes out larger
// than `maxBytes`, JPEG (on a white background) is used instead. Returns an
// empty string for a null image.
QString encodeImageForModel(const QImage &image, int maxSide = 1568,
                            int maxBytes = 3 * 1024 * 1024);

// A small preview of the image for the chat (not sent anywhere).
QImage thumbnailFor(const QImage &image, int maxSide = 96);
