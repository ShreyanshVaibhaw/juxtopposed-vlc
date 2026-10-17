// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_SEEK_PREVIEW_IMAGE_PROVIDER_HPP
#define VLC_QT_SEEK_PREVIEW_IMAGE_PROVIDER_HPP

#include <QQuickImageProvider>
#include <QImage>
#include <QUrl>
#include <QUrlQuery>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

// Unlike data: image URLs, providers also work when Qt networking is disabled.
class SeekPreviewImageProvider final : public QQuickImageProvider
{
public:
    SeekPreviewImageProvider() : QQuickImageProvider(Image) {}

    static QUrl url(const QByteArray& png)
    {
        return QUrl(QStringLiteral("image://seekpreview/") + QString::fromLatin1(
            png.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const auto separator = id.indexOf('?');
        const auto data = separator < 0 ? id : id.left(separator);
        auto image = QImage::fromData(QByteArray::fromBase64(data.toLatin1(), QByteArray::Base64UrlEncoding), "PNG");
        if (size) *size = image.size();
        const QUrlQuery query(separator < 0 ? QString() : id.mid(separator + 1));
        if (!image.isNull() && query.queryItemValue("rounded") == "1")
        {
            const auto logicalWidth = query.queryItemValue("width").toDouble();
            const auto logicalRadius = query.queryItemValue("radius").toDouble();
            if (requestedSize.isEmpty() || !std::isfinite(logicalWidth) || logicalWidth <= 0
                || !std::isfinite(logicalRadius) || logicalRadius < 0)
                return {};
            QImage frame(requestedSize, QImage::Format_ARGB32_Premultiplied);
            if (frame.isNull()) return {};
            frame.fill(Qt::black);
            QPainter painter(&frame);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            const QRectF bounds(QPointF(0, 0), QSizeF(requestedSize));
            // Match the shader path's correction for a one-pixel export rounding error.
            const auto fitted = std::abs(image.height() - image.width() * double(requestedSize.height()) / requestedSize.width()) <= 1.01
                ? QSizeF(requestedSize) : QSizeF(image.size()).scaled(QSizeF(requestedSize), Qt::KeepAspectRatio);
            painter.drawImage(QRectF(QPointF((bounds.width() - fitted.width()) / 2, (bounds.height() - fitted.height()) / 2), fitted), image);
            const auto radius = std::min(logicalRadius * requestedSize.width() / logicalWidth,
                                         std::min(bounds.width(), bounds.height()) / 2);
            QPainterPath outside;
            outside.setFillRule(Qt::OddEvenFill);
            outside.addRect(bounds);
            outside.addRoundedRect(bounds, radius, radius);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            painter.fillPath(outside, Qt::black);
            painter.end();
            if (size) *size = frame.size();
            return frame;
        }
        if (!image.isNull() && requestedSize.isValid())
            image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return image;
    }
};

#endif
