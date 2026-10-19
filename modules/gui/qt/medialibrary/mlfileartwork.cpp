// SPDX-License-Identifier: GPL-2.0-or-later
#include "mlfileartwork.hpp"
#include "medialib.hpp"
#include "mlmediastore.hpp"
#include "maininterface/mainctx.hpp"
#include "qt.hpp"
#include <vlc_preparser.h>
#include <vlc_input_item.h>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>
#include <QTimer>

MLFileArtwork::ExportedFile::~ExportedFile()
{
    if (!imported) QFile::remove(path);
}

MLFileArtwork::MLFileArtwork(QObject* parent) : QObject(parent) {}
MLFileArtwork::~MLFileArtwork()
{
    clear();
    if (m_preparser) vlc_preparser_Delete(m_preparser);
}

void MLFileArtwork::setCtx(MainCtx* ctx)
{
    if (m_ctx == ctx) return;
    clear();
    if (m_preparser) vlc_preparser_Delete(m_preparser);
    m_preparser = nullptr;
    m_store.reset();
    m_ctx = ctx;
    m_ml = ctx ? ctx->getMediaLibrary() : nullptr;
    if (m_ml) {
        m_store = std::make_unique<MLMediaStore>(m_ml);
        connect(m_store.get(), &MLMediaStore::updated, this, &MLFileArtwork::mediaUpdated);
    }
    emit ctxChanged();
}

void MLFileArtwork::clear()
{
    ++m_generation;
    if (m_request) {
        vlc_preparser_Cancel(m_preparser, m_request);
        vlc_preparser_req_Release(m_request);
        m_request = nullptr;
    }
    m_pending.clear();
    m_requested.clear();
    m_stateRequested.clear();
    m_attempted.clear();
    if (m_store) m_store->clear();
}

void MLFileArtwork::request(const QUrl& url, bool generateVideo)
{
    if (!m_store || !url.isLocalFile()) return;
    const auto mrl = url.toString(QUrl::FullyEncoded);
    const auto previous = m_requested.constFind(mrl);
    if (previous != m_requested.cend() && (previous.value() || !generateVideo)) return;
    m_requested.insert(mrl, generateVideo);
    // External media records cache previews without adding or scanning a folder.
    m_store->insert(mrl, generateVideo);
}

void MLFileArtwork::mediaUpdated(const QString& mrl, const MLMedia& media)
{
    if (!m_requested.contains(mrl) && !m_stateRequested.contains(mrl)) return;
    emit mediaPlayedChanged(QUrl::fromEncoded(mrl.toUtf8()), media.valid() && media.playCount() > 0);
    if (!m_requested.contains(mrl)) return;
    emit mediaProgressChanged(QUrl::fromEncoded(mrl.toUtf8()), media.valid() ? media.progress() : 0.0);
    const QString artwork = media.valid() ? media.smallCover() : QString {};
    if (!artwork.isEmpty())
        emit artworkChanged(QUrl::fromEncoded(mrl.toUtf8()), artwork);
    if (!media.valid() || !m_requested[mrl] || media.type() == VLC_ML_MEDIA_TYPE_AUDIO) return;
    vlc_ml_thumbnail_status_t status;
    media.smallCover(&status);
    if (status != VLC_ML_THUMBNAIL_STATUS_MISSING && status != VLC_ML_THUMBNAIL_STATUS_FAILURE) return;
    const auto id = media.getId().id;
    if (m_attempted.contains(id)) return;
    m_attempted.insert(id);
    const auto directory = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .filePath(QStringLiteral("folder-thumbnails"));
    auto file = std::make_shared<ExportedFile>();
    file->path = QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".jpg"));
    const Work work {mrl, file, id, m_generation.load()};
    struct Setup { bool ready = false; };
    m_ml->runOnMLThread<Setup>(this, [directory](vlc_medialibrary_t*, Setup& result) {
        result.ready = QDir().mkpath(directory);
    }, [this, work](quint64, Setup& result) {
        if (work.generation != m_generation || !result.ready) return;
        m_pending.push_back(work);
        startNext();
    });
}

void MLFileArtwork::requestWatchStates(const QList<QUrl>& urls, quint64 serial)
{
    if (!m_store) {
        QTimer::singleShot(0, this, [this, serial] { emit watchStatesReady(serial); });
        return;
    }
    QStringList mrls;
    mrls.reserve(urls.size());
    for (const auto& url : urls) {
        if (!url.isLocalFile()) continue;
        const auto mrl = url.toString(QUrl::FullyEncoded);
        m_stateRequested.insert(mrl);
        mrls.append(mrl);
    }
    m_store->insertMany(mrls, [this, serial, generation = m_generation.load()] {
        if (generation == m_generation) emit watchStatesReady(serial);
    });
}

void MLFileArtwork::cancelWatchStates()
{
    m_stateRequested.clear();
    if (m_store) m_store->cancelBatch();
}

void MLFileArtwork::startNext()
{
    if (m_request || m_pending.empty() || !m_ctx) return;
    const auto work = m_pending.front();
    msg_Dbg(m_ctx->getIntf(), "Folder preview request: %s", qtu(work.mrl));
    m_pending.pop_front();
    if (!m_preparser) {
        vlc_preparser_cfg cfg {};
        cfg.types = VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES;
        cfg.max_thumbnailer_threads = 1;
        cfg.timeout = VLC_TICK_FROM_SEC(15);
        cfg.external_process = false;
        m_preparser = vlc_preparser_New(VLC_OBJECT(m_ctx->getIntf()), &cfg);
    }
    struct Request { MLFileArtwork* owner; Work work; };
    static const vlc_thumbnailer_to_files_cbs callbacks = {
        [](vlc_preparser_req*, int status, const bool* results, size_t count, void* opaque) {
            std::unique_ptr<Request> request(static_cast<Request*>(opaque));
            const bool success = status == VLC_SUCCESS && count == 1 && results && results[0];
            if (!success || request->work.generation != request->owner->m_generation)
                QFile::remove(request->work.file->path);
            if (request->work.generation != request->owner->m_generation) return;
            QMetaObject::invokeMethod(request->owner, [owner = request->owner, work = request->work, success] {
                if (work.generation != owner->m_generation) { QFile::remove(work.file->path); return; }
                vlc_preparser_req_Release(owner->m_request);
                owner->m_request = nullptr;
                if (success) owner->storeArtwork(work);
                owner->startNext();
            });
        }
    };
    input_item_t* item = input_item_New(qtu(work.mrl), nullptr);
    if (m_preparser && item) {
        vlc_thumbnailer_arg args {};
        args.seek.type = decltype(args.seek)::VLC_THUMBNAILER_SEEK_POS;
        args.seek.pos = .15;
        args.seek.speed = decltype(args.seek)::VLC_THUMBNAILER_SEEK_FAST;
        vlc_thumbnailer_output output {};
        output.format = VLC_THUMBNAILER_FORMAT_JPEG;
        output.width = 512; output.height = 320; output.crop = true;
        const auto path = work.file->path.toUtf8();
        output.file_path = path.constData(); output.creat_mode = 0600;
        auto request = std::make_unique<Request>(Request {this, work});
        m_request = vlc_preparser_req_NewThumbnailToFiles(m_preparser, item, &args, &output, 1, &callbacks, request.get());
        if (m_request) {
            Request* context = request.release();
            if (vlc_preparser_Submit(m_preparser, m_request) != VLC_SUCCESS) {
                delete context;
                vlc_preparser_req_Release(m_request);
                m_request = nullptr;
            }
        }
    }
    if (item) input_item_Release(item);
    if (!m_request) QTimer::singleShot(0, this, &MLFileArtwork::startNext);
}

void MLFileArtwork::storeArtwork(const Work& work)
{
    struct Result { QString artwork; };
    m_ml->runOnMLThread<Result>(this, [work](vlc_medialibrary_t* ml, Result& result) {
        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media(ml, work.id));
        if (media) {
            // Keep artwork that became available while this decode was running.
            result.artwork = MLMedia(media.get()).smallCover();
            if (result.artwork.isEmpty()) {
                const auto uri = QUrl::fromLocalFile(work.file->path).toString(QUrl::FullyEncoded);
                vlc_ml_media_set_thumbnail(ml, work.id, qtu(uri), VLC_ML_THUMBNAIL_SMALL);
                media.reset(vlc_ml_get_media(ml, work.id));
                if (media) result.artwork = MLMedia(media.get()).smallCover();
            }
        }
        work.file->imported = QUrl::fromEncoded(result.artwork.toUtf8()) == QUrl::fromLocalFile(work.file->path);
    }, [this, work](quint64, Result& result) {
        if (!result.artwork.isEmpty() && work.generation == m_generation && m_requested.contains(work.mrl))
            emit artworkChanged(QUrl::fromEncoded(work.mrl.toUtf8()), result.artwork);
    });
}
