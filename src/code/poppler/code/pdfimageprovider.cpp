/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Anthony Granger <grangeranthony@gmail.com>
 *         Stefano Verzegnassi <stefano92.100@gmail.com
 */

#include <QQuickImageProvider>
#include <QMutexLocker>
#include <QStringList>
#include <limits>

#include "pdfimageprovider.h"

QMutex &popplerRenderMutex()
{
    static QMutex mutex;
    return mutex;
}

PdfImageProvider::PdfImageProvider(const std::shared_ptr<PdfImageProviderState> &state)
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading)
    , providerState(state)
{
}

QImage PdfImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    if (!providerState)
        return {};

    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.at(0) != QLatin1String("page"))
        return {};

    bool pageOk = false;
    const int pageNumber = parts.at(1).toInt(&pageOk);
    if (!pageOk || pageNumber < 0)
        return {};

    const bool isCropRequest = parts.size() >= 8
        && (parts.at(2) == QLatin1String("tile") || parts.at(2) == QLatin1String("region"));

    int renderWidth = 0;
    int cropX = 0;
    int cropY = 0;
    int cropWidth = 0;
    int cropHeight = 0;

    if (isCropRequest)
    {
        bool renderWidthOk = false;
        bool xOk = false;
        bool yOk = false;
        bool widthOk = false;
        bool heightOk = false;
        renderWidth = parts.at(3).toInt(&renderWidthOk);
        cropX = parts.at(4).toInt(&xOk);
        cropY = parts.at(5).toInt(&yOk);
        cropWidth = parts.at(6).toInt(&widthOk);
        cropHeight = parts.at(7).toInt(&heightOk);

        if (!renderWidthOk || !xOk || !yOk || !widthOk || !heightOk
            || renderWidth <= 0 || cropX < 0 || cropY < 0 || cropWidth <= 0 || cropHeight <= 0)
            return {};
    }

    QMutexLocker locker(&providerState->mutex);

    // Check the image cache before touching Poppler. Zooming and panning
    // revisit regions frequently, and creating a Page object is needlessly
    // expensive for an image that is already available.
    if (isCropRequest)
    {
        if (QImage *cached = providerState->tileCache.object(id))
        {
            if (size)
                *size = cached->size();
            return *cached;
        }
    }

    QMutexLocker renderLocker(&popplerRenderMutex());
    Poppler::Document *document = providerState->document;
    if (!document)
        return {};

    std::unique_ptr<Poppler::Page> page = document->page(pageNumber);
    if (!page)
        return {};

    const QSizeF pageSize = page->pageSizeF();
    if (pageSize.width() <= 0 || pageSize.height() <= 0)
        return {};

    if (isCropRequest)
    {
        const double resolution = renderWidth / (pageSize.width() / 72.0);
        const int renderedWidth = qMax(1, qRound(pageSize.width() / 72.0 * resolution));
        const int renderedHeight = qMax(1, qRound(pageSize.height() / 72.0 * resolution));
        const int tileX = qBound(0, cropX, renderedWidth);
        const int tileY = qBound(0, cropY, renderedHeight);
        const int tileWidth = qMin(cropWidth, renderedWidth - tileX);
        const int tileHeight = qMin(cropHeight, renderedHeight - tileY);

        if (tileWidth <= 0 || tileHeight <= 0)
            return {};

        QImage result = page->renderToImage(resolution, resolution,
                                             tileX, tileY, tileWidth, tileHeight);
        if (size)
            *size = result.size();

        if (!result.isNull())
        {
            const qsizetype bytes = result.sizeInBytes();
            const int cost = static_cast<int>(qMin<qsizetype>(bytes, std::numeric_limits<int>::max()));
            if (cost > 0)
                providerState->tileCache.insert(id, new QImage(result), cost);
        }

        return result;
    }

    const int requestedWidth = requestedSize.width();
    if (requestedWidth <= 0)
        return {};

    const double resolution = requestedWidth / (pageSize.width() / 72.0);
    QImage result = page->renderToImage(resolution, resolution);
    if (size)
        *size = result.size();
    return result;
}
