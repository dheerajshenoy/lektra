#pragma once

#include "Model.hpp"

#include <QUndoCommand>

// Moves or resizes an annotation.
class AnnotGeometryCommand : public QUndoCommand
{
public:
    AnnotGeometryCommand(Model *model, int pageno, int objNum,
                         Model::AnnotGeometry before,
                         Model::AnnotGeometry after,
                         QUndoCommand *parent = nullptr)
        : QUndoCommand(QObject::tr("Move Annotation"), parent), m_model(model),
          m_pageno(pageno), m_objNum(objNum), m_before(std::move(before)),
          m_after(std::move(after))
    {
    }

    void undo() override
    {
        m_model->setAnnotGeometry(m_pageno, m_objNum, m_before);
    }

    void redo() override
    {
        m_model->setAnnotGeometry(m_pageno, m_objNum, m_after);
    }

private:
    Model *m_model;
    int m_pageno;
    int m_objNum;
    Model::AnnotGeometry m_before;
    Model::AnnotGeometry m_after;
};
