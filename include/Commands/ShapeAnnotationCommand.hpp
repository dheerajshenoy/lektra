#pragma once

#include "Model.hpp"

#include <QUndoCommand>
#include <vector>
extern "C"
{
#include <mupdf/pdf.h>
}

// Adds an ellipse (CIRCLE, from `rect`), a polygon (from `vertices`) or an
// inline note (FREE_TEXT, `text` in `rect`).
class ShapeAnnotationCommand : public QUndoCommand
{
public:
    ShapeAnnotationCommand(Model *model, int pageno, enum pdf_annot_type type,
                           const fz_rect &rect,
                           std::vector<fz_point> vertices = {},
                           const QString &text            = {},
                           QUndoCommand *parent           = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno), m_type(type),
          m_rect(rect), m_vertices(std::move(vertices)), m_text(text)
    {
    }

    // The annotation that was made (-1 if it could not be)
    int objNum() const noexcept
    {
        return m_objNum;
    }

    void undo() override
    {
        // The text or comment may have been edited since.
        m_text = m_model->getAnnotComment(m_pageno, m_objNum);
        m_model->removeAnnotations(m_pageno, {m_objNum});
    }

    void redo() override
    {
        m_objNum = m_model->addShapeAnnotation(m_pageno, m_type, m_rect,
                                               m_vertices, m_text);
    }

private:
    Model *m_model;
    int m_pageno;
    enum pdf_annot_type m_type;
    fz_rect m_rect;
    std::vector<fz_point> m_vertices;
    QString m_text;
    int m_objNum{-1};
};
