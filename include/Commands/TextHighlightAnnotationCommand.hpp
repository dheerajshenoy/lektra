#pragma once

// This class represents a command to add a text highlight annotation to a PDF
// document using the MuPDF library. It supports undo and redo functionality by
// inheriting from QUndoCommand. The command stores the necessary information to
// create and remove the annotation, including the page number, quad points,
// color, and object numbers. Each quad creates a separate annotation to avoid
// visual issues with multi-line highlights.

#include "Model.hpp"

#include <QUndoCommand>
#include <vector>

extern "C"
{
#include <mupdf/pdf.h>
}

class TextHighlightAnnotationCommand : public QUndoCommand
{
public:
    TextHighlightAnnotationCommand(Model *model, int pageno,
                                   const std::vector<fz_quad> &quads,
                                   const QString &comment = {},
                                   QUndoCommand *parent   = nullptr,
                                   const QColor &color    = {},
                                   Model::TextMarkup kind
                                   = Model::TextMarkup::Highlight)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno),
          m_quads(quads), m_comment(comment), m_color(color), m_kind(kind)
    {
    }

    // The annotation that was made (-1 if it could not be)
    int objNum() const noexcept
    {
        return m_objNum;
    }

    void undo() override
    {
        m_model->removeAnnotations(m_pageno, {m_objNum});
    }

    void redo() override
    {
        switch (m_kind)
        {
            case Model::TextMarkup::Underline:
                m_objNum = m_model->addUnderlineAnnotation(m_pageno, m_quads,
                                                           m_color, m_comment);
                break;
            case Model::TextMarkup::Squiggly:
                m_objNum = m_model->addMarkupAnnotation(
                    m_pageno, PDF_ANNOT_SQUIGGLY, m_quads, m_color, m_comment);
                break;
            case Model::TextMarkup::StrikeOut:
                m_objNum = m_model->addMarkupAnnotation(
                    m_pageno, PDF_ANNOT_STRIKE_OUT, m_quads, m_color,
                    m_comment);
                break;
            default:
                m_objNum = m_model->addHighlightAnnotation(
                    m_pageno, m_quads, m_color, m_comment);
                break;
        }
    }

private:
    Model *m_model;
    int m_pageno;
    std::vector<fz_quad> m_quads;
    QString m_comment;
    QColor m_color;
    Model::TextMarkup m_kind;
    int m_objNum{-1};
};
