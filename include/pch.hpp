// Precompiled header: third-party and standard headers that most of the
// sources include. Only things that rarely change belong here -- touching a
// header listed in this file rebuilds everything.
#pragma once

#ifdef __cplusplus

    // --- Standard library ---
    #include <algorithm>
    #include <array>
    #include <cmath>
    #include <cstdint>
    #include <functional>
    #include <map>
    #include <memory>
    #include <optional>
    #include <set>
    #include <string>
    #include <string_view>
    #include <unordered_map>
    #include <utility>
    #include <vector>

    // --- Qt Core ---
    #include <QByteArray>
    #include <QColor>
    #include <QDir>
    #include <QFile>
    #include <QFileInfo>
    #include <QHash>
    #include <QJsonArray>
    #include <QJsonDocument>
    #include <QJsonObject>
    #include <QList>
    #include <QMap>
    #include <QObject>
    #include <QPointF>
    #include <QRectF>
    #include <QSet>
    #include <QSize>
    #include <QStandardPaths>
    #include <QString>
    #include <QStringList>
    #include <QTimer>
    #include <QUrl>
    #include <QVariant>
    #include <QtConcurrent/QtConcurrent>

    // --- Qt Gui ---
    #include <QFont>
    #include <QFontMetrics>
    #include <QImage>
    #include <QKeyEvent>
    #include <QMouseEvent>
    #include <QPainter>
    #include <QPixmap>
    #include <QShortcut>
    #include <QUndoCommand>

    // --- Qt Widgets ---
    #include <QApplication>
    #include <QDialog>
    #include <QGraphicsItem>
    #include <QGraphicsScene>
    #include <QGraphicsView>
    #include <QHBoxLayout>
    #include <QLabel>
    #include <QLineEdit>
    #include <QMenu>
    #include <QMessageBox>
    #include <QPushButton>
    #include <QScrollBar>
    #include <QVBoxLayout>
    #include <QWidget>

    // --- MuPDF ---
    #include <mupdf/fitz.h>
    #include <mupdf/pdf.h>

#endif // __cplusplus
