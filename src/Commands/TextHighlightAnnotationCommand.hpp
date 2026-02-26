#pragma once

// This class represents a command to add a text highlight annotation to a PDF
// document using the MuPDF library. It supports undo and redo functionality by
// inheriting from QUndoCommand. The command stores the necessary information to
// create and remove the annotation, including the page number, quad points,
// color, and object numbers. Each quad creates a separate annotation to avoid
// visual issues with multi-line highlights.

#include "../Model.hpp"

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
                                   QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno), m_quads(quads)
    {
    }

    void undo() override
    {
        m_model->removeAnnotations(m_pageno, {m_objNum});
    }

    void redo() override
    {
        m_objNum = m_model->addHighlightAnnotation(m_pageno, m_quads);
    }

private:
    Model *m_model;
    int m_pageno;
    std::vector<fz_quad> m_quads;
    int m_objNum{-1};
};
