/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * Authors: Prince Gupta <guptaprince8832@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/


#include "mlmediastore.hpp"

#include "mlhelper.hpp"
#include "medialib.hpp"
#include "mlmedia.hpp"
#include <QTimer>

static const char *UPDATE_QUEUE = "MLMEDIASTORE_UPDATEQUEUE";

MLMediaStore::~MLMediaStore() = default;

MLMediaStore::MLMediaStore(MediaLib *ml, QObject *parent)
    : QObject(parent)
    , m_ml {ml}
    , m_ml_event_handle( nullptr, [this](vlc_ml_event_callback_t* cb )
    {
        assert( m_ml != nullptr );
        m_ml->unregisterEventListener( cb );
    })
{
    m_ml_event_handle.reset
    (
        m_ml->registerEventListener(MLMediaStore::onVlcMlEvent, this)
    );
}

void MLMediaStore::insert(const QString &mrl, bool createMissing)
{
    struct Ctx
    {
        MLMedia media;
    };

    m_files.insert(mrl);
    const auto revision = ++m_revisions[mrl];

    m_ml->runOnMLThread<Ctx>(this,
    //ML thread
    [mrl, createMissing](vlc_medialibrary_t *ml, Ctx &ctx)
    {
        ml_unique_ptr<vlc_ml_media_t> media {vlc_ml_get_media_by_mrl(ml, qtu(mrl))};
        if (!media && createMissing)
            media.reset(vlc_ml_new_external_media(ml, qtu(mrl)));
        if (media)
            ctx.media = MLMedia(media.get());
    },
    //UI thread
    [this, mrl, revision, generation = m_generation](quint64, Ctx &ctx)
    {
        if (generation != m_generation || m_revisions.value(mrl) != revision)
            return;
        if (!ctx.media.valid()) {
            emit updated(mrl, ctx.media);
            return;
        }
        setMedia(mrl, std::move(ctx.media));
    });
}

void MLMediaStore::remove(const MLItemId &id)
{
    if (!m_mrls.contains(id))
        return;

    const QString mrl = m_mrls[id];

    m_mrls.remove(id);
    m_files.remove(mrl);
    m_revisions.remove(mrl);
}

void MLMediaStore::insertMany(const QStringList &mrls, std::function<void()> completed)
{
    auto batch = std::make_shared<Batch>();
    batch->generation = m_generation;
    batch->completed = std::move(completed);
    QSet<QString> unique;
    batch->entries.reserve(mrls.size());
    for (const auto& mrl : mrls) {
        if (unique.contains(mrl)) continue;
        unique.insert(mrl);
        m_files.insert(mrl);
        batch->entries.push_back({mrl, ++m_revisions[mrl]});
    }
    // Replace queued work, but let the one in-flight chunk finish before starting
    // another. Rapid searches cannot build up a queue of database jobs.
    cancelBatch();
    m_batch = std::move(batch);
    QTimer::singleShot(0, this, &MLMediaStore::readNextBatch);
}

void MLMediaStore::readNextBatch()
{
    if (m_batchRunning || !m_batch) return;
    const auto batch = m_batch;
    if (batch->offset == batch->entries.size()) {
        m_batch.reset();
        if (batch->completed) batch->completed();
        return;
    }
    const auto chunk = batch->entries.mid(batch->offset, 64);
    m_batchRunning = true;
    m_ml->runOnMLThread<QVector<MLMedia>>(this,
        [batch, chunk](vlc_medialibrary_t* ml, QVector<MLMedia>& result) {
            result.reserve(chunk.size());
            for (const auto& entry : chunk) {
                if (batch->cancelled) break;
                ml_unique_ptr<vlc_ml_media_t> media {vlc_ml_get_media_by_mrl(ml, qtu(entry.mrl))};
                result.push_back(media ? MLMedia(media.get()) : MLMedia {});
            }
        },
        [this, batch, chunk](quint64, QVector<MLMedia>& result) {
            m_batchRunning = false;
            if (batch == m_batch && batch->generation == m_generation) {
                for (qsizetype i = 0; i < chunk.size(); ++i) {
                    if (batch != m_batch || batch->generation != m_generation) break;
                    const auto& entry = chunk[i];
                    if (m_revisions.value(entry.mrl) != entry.revision) continue;
                    if (result[i].valid()) setMedia(entry.mrl, std::move(result[i]));
                    else emit updated(entry.mrl, result[i]);
                }
                batch->offset += chunk.size();
            }
            QTimer::singleShot(0, this, &MLMediaStore::readNextBatch);
        });
}

void MLMediaStore::cancelBatch()
{
    if (m_batch) m_batch->cancelled = true;
    m_batch.reset();
}

void MLMediaStore::clear()
{
    ++m_generation;
    cancelBatch();
    m_files.clear();
    m_mrls.clear();
    m_revisions.clear();
}

void MLMediaStore::onVlcMlEvent(void *data, const vlc_ml_event_t *event)
{
    auto self = static_cast<MLMediaStore*>(data);
    switch (event->i_type)
    {
    case VLC_ML_EVENT_MEDIA_ADDED:
    case VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED:
    {
        if (event->i_type == VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED
                && event->media_thumbnail_generated.i_size != VLC_ML_THUMBNAIL_SMALL)
            break;
        const vlc_ml_media_t *media = event->i_type == VLC_ML_EVENT_MEDIA_ADDED
                ? event->creation.p_media : event->media_thumbnail_generated.p_media;
        QString mrl;
        for (const vlc_ml_file_t &file : ml_range_iterate<vlc_ml_file_t>(media->p_files))
        {
            if (file.i_type == VLC_ML_FILE_TYPE_MAIN)
            {
                mrl = QString::fromUtf8(file.psz_mrl);
                break;
            }
        }

        if (mrl.isEmpty())
            break;

        MLMedia mlMedia (media);
        QMetaObject::invokeMethod(self, [self, mrl, mlMedia]()
        {
            self->setMedia(mrl, mlMedia);
        });

        break;
    }

    case VLC_ML_EVENT_MEDIA_UPDATED:
    {
        const MLItemId id(event->modification.i_entity_id, VLC_ML_PARENT_UNKNOWN);
        QMetaObject::invokeMethod(self, [self, id] () mutable
        {
            self->update(id);
        });

        break;
    }

    case VLC_ML_EVENT_MEDIA_DELETED:
    {
        const MLItemId id{ event->deletion.i_entity_id, VLC_ML_PARENT_UNKNOWN };
        QMetaObject::invokeMethod(self, [self, id]()
        {
            self->remove(id);
        });

        break;
    }

    default:
        break;
    }
}

void MLMediaStore::update(const MLItemId &id)
{
    if (!m_mrls.contains(id))
        return;

    struct Ctx
    {
        MLMedia media;
    };

    const auto mrl = m_mrls[id];
    const auto revision = ++m_revisions[mrl];

    m_ml->runOnMLThread<Ctx>(this,
    //ML thread
    [id](vlc_medialibrary_t *ml, Ctx &ctx)
    {
        ml_unique_ptr<vlc_ml_media_t> media {vlc_ml_get_media(ml, id.id)};
        ctx.media = media ? MLMedia(media.get()) : MLMedia {};
    },
    //UI thread
    [this, id, mrl, revision, generation = m_generation](quint64, Ctx &ctx)
    {
        if (generation != m_generation || m_revisions.value(mrl) != revision || !m_mrls.contains(id))
            return; // item was removed?

        emit updated(mrl, ctx.media);
    }, UPDATE_QUEUE); // update in a single queue in case there
                      // is a overlap of same media update
}

void MLMediaStore::setMedia(const QString &mrl, MLMedia media)
{
    if (!m_files.contains(mrl))
        return;

    // A newer event must win over a lookup started before that event.
    ++m_revisions[mrl];
    m_mrls[media.getId()] = mrl;
    emit updated(mrl, media);
}
