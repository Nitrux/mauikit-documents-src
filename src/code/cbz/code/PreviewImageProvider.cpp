/*
 * Copyright (C) 2015 Dan Leinir Turthra Jensen <admin@leinir.dk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "PreviewImageProvider.h"

#include <KIO/PreviewJob>
#include <KFileItem>
#include <kiconloader.h>

#include <QGuiApplication>
#include <QIcon>
#include <QMimeDatabase>
#include <QPointer>
#include <QDebug>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QImage mimeIconFallback(const QString &mimetype, const QSize &requestedSize)
{
    QMimeDatabase db;
    const QString iconName = db.mimeTypeForName(mimetype).iconName();
    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("text-x-generic"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("application-octet-stream"));
    if (icon.isNull())
        return QImage();
    const QSize s = (requestedSize.width() > 0 && requestedSize.height() > 0)
                    ? requestedSize : QSize(128, 128);
    return icon.pixmap(s).toImage();
}

// ---------------------------------------------------------------------------
// PreviewImageProvider
// ---------------------------------------------------------------------------

class PreviewImageProvider::Private
{
public:
    Private() {}
};

PreviewImageProvider::PreviewImageProvider()
    : QQuickAsyncImageProvider()
    , d(new Private)
{
}

PreviewImageProvider::~PreviewImageProvider()
{
    delete d;
}

// ---------------------------------------------------------------------------
// PreviewResponse
//
// KIO::PreviewJob must be created on the main thread so the Qt event loop
// can deliver its signals.  The old QRunnable approach broke because worker
// threads have no event loop.
//
// Pattern mirrors mauikit-filebrowsing's Thumbnailer exactly:
//   • gotPreview  → store scaled image, emit QQuickImageResponse::finished()
//   • failed      → store mime-icon fallback, emit finished()
//   • NO connection to KJob::finished — that signal can fire during kill()
//     (cancel) causing use-after-free if QML has already begun tearing down
//     the response.
// ---------------------------------------------------------------------------

class PreviewResponse : public QQuickImageResponse
{
public:
    PreviewResponse(const QString &id, const QSize &requestedSize)
        : m_requestedSize(requestedSize)
    {
        KIO::PreviewJob::setDefaultDevicePixelRatio(qApp->devicePixelRatio());

        const QSize jobSize = (requestedSize.width() > 0 && requestedSize.height() > 0)
                              ? requestedSize
                              : QSize(KIconLoader::SizeEnormous, KIconLoader::SizeEnormous);

        QStringList plugins = KIO::PreviewJob::availablePlugins();
        m_job = new KIO::PreviewJob(
            KFileItemList() << KFileItem(QUrl::fromUserInput(id)),
            jobSize, &plugins);
        m_job->setIgnoreMaximumSize(true);
        m_job->setScaleType(KIO::PreviewJob::ScaledAndCached);

        // Use 'this' as context so Qt auto-disconnects if we are destroyed
        // before the signal fires (e.g. rapid scrolling).
        connect(m_job, &KIO::PreviewJob::gotPreview, this,
                [this](const KFileItem &, const QPixmap &pixmap) {
            m_image = pixmap.toImage();
            if (!m_image.isNull() && m_requestedSize.width() > 0 && m_requestedSize.height() > 0)
                m_image = m_image.scaled(m_requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            Q_EMIT finished();
        });

        connect(m_job, &KIO::PreviewJob::failed, this,
                [this](const KFileItem &item) {
            m_image = mimeIconFallback(item.mimetype(), m_requestedSize);
            Q_EMIT finished();
        });

        m_job->start();
    }

    void cancel() override
    {
        // Kill quietly: suppress result/finished signals so they cannot fire
        // after QML has begun destroying this response.
        if (m_job)
            m_job->kill(KJob::Quietly);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QPointer<KIO::PreviewJob> m_job;
    QImage m_image;
    QSize m_requestedSize;
};

QQuickImageResponse *PreviewImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // Strip any leading double-slashes that can arrive in the id.
    QString adjustedId = id;
    while (adjustedId.startsWith(QLatin1String("//")))
        adjustedId = adjustedId.mid(1);
    return new PreviewResponse(adjustedId, requestedSize);
}
