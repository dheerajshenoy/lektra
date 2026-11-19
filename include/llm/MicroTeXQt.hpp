#include "config.h"

// Derived from MicroTeX's Qt backend (thirdparty/microtex/src/platform/qt,
// MIT licence). Changed: text is drawn as glyph outlines (QPainterPath) at a
// large size and scaled down. MicroTeX uses 1-unit fonts under a scaling
// painter, which Qt 6 draws as nothing.

#if defined(BUILD_QT) && !defined(MEM_CHECK)

    #ifndef GRAPHIC_QT_H_INCLUDED
        #define GRAPHIC_QT_H_INCLUDED

        #include "graphic/graphic.h"

        #include <QBrush>
        #include <QFont>
        #include <QMap>
        #include <QPainter>
        #include <QString>
        #include <string>

namespace tex
{

// remove a null byte off end of QString
QString
wstring_to_QString(const std::wstring &ws);

class Font_qt : public Font
{

private:
    QFont _font;

    static QMap<QString, QString> _loaded_families;

public:
    Font_qt(const std::string &family = "", int style = PLAIN,
            float size = 1.f);

    Font_qt(const std::string &file, float size);

    std::string getFamily() const;

    int getStyle() const;

    QFont getQFont() const;

    float getSize() const override;

    sptr<Font> deriveFont(int style) const override;

    bool operator==(const Font &f) const override;

    bool operator!=(const Font &f) const override;

    ~Font_qt() override {};
};

/**************************************************************************************************/

class TextLayout_qt : public TextLayout
{
private:
    QFont _font;
    QString _text;

public:
    TextLayout_qt(const std::wstring &src, const sptr<Font_qt> &font);

    void getBounds(Rect &r) override;

    void draw(Graphics2D &g2, float x, float y) override;
};

/**************************************************************************************************/

class Graphics2D_qt : public Graphics2D
{
private:
    /*static*/ Font_qt _default_font = Font_qt("SansSerif", PLAIN, 20.f);

    QPainter *_painter;

    color _color;
    Stroke _stroke;
    const Font_qt *_font;
    float _sx, _sy;

    void setPen();

public:
    Graphics2D_qt(QPainter *painter);

    QBrush getQBrush() const;

    QPainter *getQPainter() const;

    void setColor(color c) override;

    color getColor() const override;

    void setStroke(const Stroke &s) override;

    const Stroke &getStroke() const override;

    void setStrokeWidth(float w) override;

    const Font *getFont() const override;

    void setFont(const Font *font) override;

    void translate(float dx, float dy) override;

    void scale(float sx, float sy) override;

    void rotate(float angle) override;

    void rotate(float angle, float px, float py) override;

    void reset() override;

    float sx() const override;

    float sy() const override;

    void drawChar(wchar_t c, float x, float y) override;

    void drawText(const std::wstring &t, float x, float y) override;

    void drawLine(float x, float y1, float x2, float y2) override;

    void drawRect(float x, float y, float w, float h) override;

    void fillRect(float x, float y, float w, float h) override;

    void drawRoundRect(float x, float y, float w, float h, float rx,
                               float ry) override;

    void fillRoundRect(float x, float y, float w, float h, float rx,
                               float ry) override;
};

} // namespace tex

    #endif // GRAPHIC_QT_H_INCLUDED
#endif     // defined(BUILD_QT) && !defined(MEM_CHECK)
