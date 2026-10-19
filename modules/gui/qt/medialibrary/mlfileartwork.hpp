// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <QHash>
#include <QSet>
#include <memory>
#include <cstdint>
#include <deque>
#include <atomic>

class MainCtx;
class MediaLib;
class MLMedia;
class MLMediaStore;
struct vlc_preparser_t;
struct vlc_preparser_req;
Q_MOC_INCLUDE("maininterface/mainctx.hpp")

/** Lazy file artwork using the media library's persistent thumbnail cache. */
class MLFileArtwork : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MainCtx* ctx READ ctx WRITE setCtx NOTIFY ctxChanged FINAL)
public:
    explicit MLFileArtwork(QObject* parent = nullptr);
    ~MLFileArtwork() override;
    MainCtx* ctx() const { return m_ctx; }
    void setCtx(MainCtx* ctx);
    Q_INVOKABLE void request(const QUrl& url, bool generateVideo);
    Q_INVOKABLE void clear();
signals:
    void ctxChanged();
    void artworkChanged(const QUrl& url, const QString& artwork);
    void mediaProgressChanged(const QUrl& url, qreal progress);
private:
    void mediaUpdated(const QString& mrl, const MLMedia& media);
    struct ExportedFile {
        QString path;
        bool imported = false;
        ~ExportedFile();
    };
    struct Work { QString mrl; std::shared_ptr<ExportedFile> file; int64_t id; quint64 generation; };
    void startNext();
    void storeArtwork(const Work& work);
    MainCtx* m_ctx = nullptr;
    MediaLib* m_ml = nullptr;
    std::unique_ptr<MLMediaStore> m_store;
    QHash<QString, bool> m_requested;
    QSet<int64_t> m_attempted;
    std::deque<Work> m_pending;
    vlc_preparser_t* m_preparser = nullptr;
    vlc_preparser_req* m_request = nullptr;
    std::atomic<quint64> m_generation {0};
};
