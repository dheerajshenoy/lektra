#pragma once

#include <QPalette>
#include <QString>

// Lua source as HTML with syntax colours, for display in a QLabel. The colours
// are chosen for a dark or a light background, whichever the palette has.
// Keywords, strings, comments, numbers, the names Lektra scripts use most and
// function calls each get their own colour.
QString
luaToHtml(const QString &code, const QPalette &palette);

// The same wrapper without highlighting, for other languages and plain output.
QString
plainCodeToHtml(const QString &code);
