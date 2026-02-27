#pragma once

// This class represents a command to delete multiple annotations from a PDF
// document using the MuPDF library. It supports undo and redo functionality by
// inheriting from QUndoCommand. The command stores the necessary information to
// recreate all deleted annotations on undo.

#include "../Model.hpp"

#include <QSet>
#include <QUndoCommand>
extern "C"
{
#include <mupdf/pdf.h>
}

class DeleteAnnotationsCommand : public QUndoCommand
{
public:
    DeleteAnnotationsCommand(Model *model, int pageno, const QSet<int> &objNums,
                             QUndoCommand *parent = nullptr)
        : QUndoCommand(objNums.size() == 1 ? "Delete Annotation"
                                           : "Delete Annotations",
                       parent),
          m_model(model), m_pageno(pageno)
    {
        // Capture annotation data up front so undo can recreate them
        captureAnnotationsData(objNums);
    }

    void undo() override
    {
        // Recreate each deleted annotation via Model and record the new objNums
        for (AnnotData &d : m_annotations)
        {
            switch (d.type)
            {
                case PDF_ANNOT_HIGHLIGHT:
                    d.objNum
                        = m_model->addHighlightAnnotation(m_pageno, d.quads);
                    break;
                case PDF_ANNOT_SQUARE:
                    d.objNum = m_model->addRectAnnotation(m_pageno, d.rect);
                    break;
                case PDF_ANNOT_TEXT:
                    d.objNum = m_model->addTextAnnotation(m_pageno, d.rect,
                                                          d.contents);
                    break;
                default:
                    break;
            }
        }
    }

    void redo() override
    {
        std::vector<int> objNums;
        objNums.reserve(m_annotations.size());
        for (const AnnotData &d : m_annotations)
            if (d.objNum >= 0)
                objNums.push_back(d.objNum);

        m_model->removeAnnotations(m_pageno, objNums);
    }

private:
    struct AnnotData
    {
        int objNum{-1};
        enum pdf_annot_type type
        {
            PDF_ANNOT_UNKNOWN
        };
        fz_rect rect{};
        float color[4]{0, 0, 0, 1};
        float opacity{1.0f};
        std::vector<fz_quad> quads;
        QString contents;
    };

    void captureAnnotationsData(const QSet<int> &objNums)
    {
        if (!m_model || objNums.isEmpty())
            return;

        fz_context *ctx   = m_model->m_ctx;
        pdf_document *pdf = pdf_specifics(ctx, m_model->m_doc);
        if (!pdf)
            return;

        fz_try(ctx)
        {
            pdf_page *page = pdf_load_page(ctx, pdf, m_pageno);
            if (!page)
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");

            for (pdf_annot *a = pdf_first_annot(ctx, page); a;
                 a            = pdf_next_annot(ctx, a))
            {
                const int num = pdf_to_num(ctx, pdf_annot_obj(ctx, a));
                if (!objNums.contains(num))
                    continue;

                AnnotData d;
                d.objNum  = num;
                d.type    = pdf_annot_type(ctx, a);
                d.opacity = pdf_annot_opacity(ctx, a);
                int n     = 0;

                switch (d.type)
                {
                    case PDF_ANNOT_HIGHLIGHT:
                        pdf_annot_color(ctx, a, &n, d.color);
                        d.color[3] = d.opacity;
                        for (int i = 0, c = pdf_annot_quad_point_count(ctx, a);
                             i < c; ++i)
                            d.quads.push_back(pdf_annot_quad_point(ctx, a, i));
                        break;

                    case PDF_ANNOT_SQUARE:
                        pdf_annot_interior_color(ctx, a, &n, d.color);
                        d.color[3] = d.opacity;
                        d.rect     = pdf_annot_rect(ctx, a);
                        break;

                    case PDF_ANNOT_TEXT:
                        pdf_annot_color(ctx, a, &n, d.color);
                        d.color[3] = d.opacity;
                        d.rect     = pdf_annot_rect(ctx, a);
                        if (const char *c = pdf_annot_contents(ctx, a))
                            d.contents = QString::fromUtf8(c);
                        break;

                    default:
                        d.rect = pdf_annot_rect(ctx, a);
                        break;
                }

                m_annotations.push_back(std::move(d));
            }

            fz_drop_page(ctx, (fz_page *)page);
        }
        fz_catch(ctx)
        {
            qWarning() << "DeleteAnnotationsCommand: failed to capture:"
                       << fz_caught_message(ctx);
        }
    }

    Model *m_model;
    int m_pageno;
    std::vector<AnnotData> m_annotations;
};
