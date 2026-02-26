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
    // Structure to hold captured annotation data
    struct AnnotationData
    {
        int objNum               = -1;
        enum pdf_annot_type type = PDF_ANNOT_UNKNOWN;
        fz_rect rect;
        float color[4] = {0, 0, 0, 1};

        std::vector<fz_quad> quads; // For highlight annotations
        QString contents;           // For text annotations
    };

    // Constructor that captures annotations by their objNums before deletion
    // Note: indexes here are actually PDF object numbers (objNums), not
    // iteration indices
    DeleteAnnotationsCommand(Model *model, int pageno, const QSet<int> &objNums,
                             QUndoCommand *parent = nullptr)
        : QUndoCommand("Delete Annotations", parent), m_model(model),
          m_pageno(pageno)
    {
        if (objNums.size() == 1)
            setText("Delete Annotation");

        // Capture annotation data for all selected annotations by objNum
        captureAnnotationsDataByObjNum(objNums);
    }

    void undo() override
    {
        if (!m_model || m_annotations.empty())
            return;

        fz_context *ctx   = m_model->m_ctx;
        fz_document *doc  = m_model->m_doc;
        pdf_document *pdf = pdf_specifics(ctx, doc);

        if (!pdf)
            return;

        fz_try(ctx)
        {
            pdf_page *page = pdf_load_page(ctx, pdf, m_pageno);
            if (!page)
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");

            // Recreate all annotations
            for (AnnotationData &data : m_annotations)
            {
                pdf_annot *annot = nullptr;

                switch (data.type)
                {
                    case PDF_ANNOT_HIGHLIGHT:
                    {
                        annot
                            = pdf_create_annot(ctx, page, PDF_ANNOT_HIGHLIGHT);
                        if (annot && !data.quads.empty())
                        {
                            pdf_set_annot_quad_points(ctx, annot,
                                                      data.quads.size(),
                                                      data.quads.data());
                            pdf_set_annot_color(ctx, annot, 3, data.color);
                            pdf_set_annot_opacity(ctx, annot, data.color[3]);
                        }
                    }
                    break;

                    case PDF_ANNOT_SQUARE:
                    {
                        annot = pdf_create_annot(ctx, page, PDF_ANNOT_SQUARE);
                        if (annot)
                        {
                            pdf_set_annot_rect(ctx, annot, data.rect);
                            pdf_set_annot_interior_color(ctx, annot, 3,
                                                         data.color);
                            pdf_set_annot_opacity(ctx, annot, data.color[3]);
                        }
                    }
                    break;

                    case PDF_ANNOT_TEXT:
                    {
                        annot = pdf_create_annot(ctx, page, PDF_ANNOT_TEXT);
                        if (annot)
                        {
                            pdf_set_annot_rect(ctx, annot, data.rect);
                            pdf_set_annot_color(ctx, annot, 3, data.color);
                            pdf_set_annot_opacity(ctx, annot, data.color[3]);
                            if (!data.contents.isEmpty())
                            {
                                pdf_set_annot_contents(
                                    ctx, annot,
                                    data.contents.toUtf8().constData());
                            }
                        }
                    }
                    break;

                    default:
                        break;
                }

                if (annot)
                {
                    pdf_update_annot(ctx, annot);
                    // Update the stored object number to the new annotation
                    pdf_obj *obj = pdf_annot_obj(ctx, annot);
                    data.objNum  = pdf_to_num(ctx, obj);
                    pdf_drop_annot(ctx, annot);
                }
            }

            fz_drop_page(ctx, (fz_page *)page);
        }
        fz_catch(ctx)
        {
            qWarning() << "Undo delete failed:" << fz_caught_message(ctx);
        }

        m_model->invalidatePageCache(m_pageno);
        emit m_model->reloadRequested(m_pageno);
    }

    void redo() override
    {
        if (!m_model || m_annotations.empty())
            return;

        fz_context *ctx   = m_model->m_ctx;
        fz_document *doc  = m_model->m_doc;
        pdf_document *pdf = pdf_specifics(ctx, doc);

        if (!pdf)
            return;

        fz_try(ctx)
        {
            pdf_page *page = pdf_load_page(ctx, pdf, m_pageno);
            if (!page)
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");

            // Delete annotations by objNum
            for (const AnnotationData &data : m_annotations)
            {
                if (data.objNum < 0)
                    continue;

                pdf_annot *annot = pdf_first_annot(ctx, page);
                while (annot)
                {
                    pdf_obj *obj = pdf_annot_obj(ctx, annot);
                    if (pdf_to_num(ctx, obj) == data.objNum)
                    {
                        pdf_delete_annot(ctx, page, annot);
                        pdf_update_page(ctx, page);
                        break;
                    }
                    annot = pdf_next_annot(ctx, annot);
                }
            }

            fz_drop_page(ctx, (fz_page *)page);
        }
        fz_catch(ctx)
        {
            qWarning() << "Redo delete failed:" << fz_caught_message(ctx);
        }

        m_model->invalidatePageCache(m_pageno);
    }

private:
    // Capture annotation data by looking up annotations by their objNum (PDF
    // object number)
    void captureAnnotationsDataByObjNum(const QSet<int> &objNums)
    {
        if (!m_model || objNums.isEmpty())
            return;

        fz_context *ctx   = m_model->m_ctx;
        fz_document *doc  = m_model->m_doc;
        pdf_document *pdf = pdf_specifics(ctx, doc);

        if (!pdf)
            return;

        fz_try(ctx)
        {
            pdf_page *page = pdf_load_page(ctx, pdf, m_pageno);
            if (!page)
                fz_throw(ctx, FZ_ERROR_GENERIC, "Failed to load page");

            pdf_annot *annot = pdf_first_annot(ctx, page);

            while (annot)
            {
                pdf_obj *obj   = pdf_annot_obj(ctx, annot);
                int thisObjNum = pdf_to_num(ctx, obj);

                // Check if this annotation's objNum is in the set we want to
                // delete
                if (objNums.contains(thisObjNum))
                {
                    AnnotationData data;
                    data.objNum = thisObjNum;
                    data.type   = pdf_annot_type(ctx, annot);

                    int n         = 0;
                    float opacity = pdf_annot_opacity(ctx, annot);

                    switch (data.type)
                    {
                        case PDF_ANNOT_HIGHLIGHT:
                        {
                            pdf_annot_color(ctx, annot, &n, data.color);
                            data.color[3] = opacity;

                            // Capture quad points
                            int quad_count
                                = pdf_annot_quad_point_count(ctx, annot);
                            data.quads.reserve(quad_count);
                            for (int i = 0; i < quad_count; ++i)
                            {
                                fz_quad q = pdf_annot_quad_point(ctx, annot, i);
                                data.quads.push_back(q);
                            }
                        }
                        break;

                        case PDF_ANNOT_SQUARE:
                        {
                            pdf_annot_interior_color(ctx, annot, &n,
                                                     data.color);
                            data.color[3] = opacity;
                            data.rect     = pdf_annot_rect(ctx, annot);
                        }
                        break;

                        case PDF_ANNOT_TEXT:
                        {
                            pdf_annot_color(ctx, annot, &n, data.color);
                            data.color[3] = opacity;
                            data.rect     = pdf_annot_rect(ctx, annot);
                            const char *contents
                                = pdf_annot_contents(ctx, annot);
                            if (contents)
                                data.contents = QString::fromUtf8(contents);
                        }
                        break;

                        default:
                            data.rect = pdf_annot_rect(ctx, annot);
                            break;
                    }

                    m_annotations.push_back(data);
                }

                annot = pdf_next_annot(ctx, annot);
            }

            fz_drop_page(ctx, (fz_page *)page);
        }
        fz_catch(ctx)
        {
            qWarning() << "Failed to capture annotation data:"
                       << fz_caught_message(ctx);
        }
    }

    Model *m_model;
    int m_pageno;
    std::vector<AnnotationData> m_annotations;
};
