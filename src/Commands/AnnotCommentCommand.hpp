#pragma once

// This class represents a command to add a comment to annotation to a PDF
// document using the MuPDF library. It supports undo and redo functionality by
// inheriting from QUndoCommand. The command stores the necessary information to
// create and remove the annotation, including the page number, quad points,
// color, and object numbers.

#include "../Model.hpp"

#include <QUndoCommand>

extern "C"
{
#include <mupdf/pdf.h>
}

class AnnotCommentCommand : public QUndoCommand
{
public:
    AnnotCommentCommand(Model *model, int pageno, int objNum,
                        const QString &oldComment, const QString &newComment,
                        QUndoCommand *parent = nullptr)
        : QUndoCommand(parent), m_model(model), m_pageno(pageno),
          m_objNum(objNum), m_oldComment(oldComment), m_newComment(newComment)
    {
    }

    void undo() override
    {
        m_model->addAnnotComment(m_pageno, m_objNum, m_oldComment);
    }

    void redo() override
    {
        m_model->addAnnotComment(m_pageno, m_objNum, m_newComment);
    }

    // Merge consecutive edits to the same annotation into one undo step
    // so rapidly re-editing a comment doesn't flood the undo stack.
    // int id() const override
    // {
    //     return m_objNum;
    // }
    // bool mergeWith(const QUndoCommand *other) override
    // {
    //     if (other->id() != id())
    //         return false;
    //     m_newComment
    //         = static_cast<const AnnotCommentCommand *>(other)->m_newComment;
    //     return true;
    // }

private:
    Model *m_model;
    int m_pageno;
    int m_objNum{-1};
    QString m_oldComment, m_newComment;
};
