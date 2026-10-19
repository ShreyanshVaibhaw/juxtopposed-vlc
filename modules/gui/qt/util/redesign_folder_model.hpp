// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_REDESIGN_FOLDER_MODEL_HPP
#define VLC_REDESIGN_FOLDER_MODEL_HPP

#include <QAbstractListModel>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QUrl>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QVariantMap>
#include <memory>

class RedesignFolderScanResult : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void ready(quint64 serial, const QFileInfoList& entries);
    void infoReady(quint64 serial, const QVariantMap& info);
    void watchSorted(quint64 serial, const QFileInfoList& entries);
};

/** Flat folder contents backed by Qt's asynchronous filesystem watcher. */
class RedesignFolderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QUrl folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters NOTIFY nameFiltersChanged FINAL)
    Q_PROPERTY(bool sortReversed READ sortReversed WRITE setSortReversed NOTIFY sortReversedChanged FINAL)
    Q_PROPERTY(QString sortCriteria READ sortCriteria WRITE setSortCriteria NOTIFY sortCriteriaChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int directoryCount READ directoryCount NOTIFY countChanged FINAL)
    Q_PROPERTY(QString searchPattern READ searchPattern WRITE setSearchPattern NOTIFY searchPatternChanged FINAL)
    Q_PROPERTY(bool watchStatesPending READ watchStatesPending NOTIFY watchStatesPendingChanged FINAL)
public:
    enum Role { FileName = Qt::UserRole + 1, FileUrl, FileIsDir, Thumbnail, Progress };
    explicit RedesignFolderModel(QObject* parent = nullptr);
    QUrl folder() const { return m_folder; }
    void setFolder(const QUrl& folder);
    QStringList nameFilters() const { return m_nameFilters; }
    void setNameFilters(const QStringList& filters);
    bool sortReversed() const { return m_sortReversed; }
    void setSortReversed(bool reversed);
    QString sortCriteria() const { return m_sortCriteria; }
    void setSortCriteria(const QString& criteria);
    int count() const { return m_entries.size(); }
    int directoryCount() const;
    QString searchPattern() const { return m_searchPattern; }
    void setSearchPattern(const QString& pattern);
    bool watchStatesPending() const { return m_watchStatesPending; }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariant get(int row, const QString& role) const;
    Q_INVOKABLE void setArtwork(const QUrl& url, const QString& artwork);
    Q_INVOKABLE void setProgress(const QUrl& url, qreal progress);
    Q_INVOKABLE void setPlayed(const QUrl& url, bool played);
    Q_INVOKABLE void finishWatchStates(quint64 serial);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE quint64 requestInfo(const QUrl& folder);
signals:
    void folderChanged();
    void nameFiltersChanged();
    void sortReversedChanged();
    void sortCriteriaChanged();
    void countChanged();
    void searchPatternChanged();
    void artworkRequested(const QUrl& url);
    void watchStatesRequested(const QList<QUrl>& urls, quint64 serial);
    void watchStatesCancelled();
    void watchStatesPendingChanged();
    void infoReady(quint64 serial, const QVariantMap& info);
private:
    void scheduleRefresh();
    void scan();
    void replaceEntries(const QFileInfoList& entries);
    void requestWatchStates();
    void setWatchStatesPending(bool pending);
    void scheduleWatchSort();
    void startWatchSort();
    void applyWatchOrder(const QFileInfoList& entries);
    void rebuildRows();
    QFileSystemWatcher m_watcher;
    std::shared_ptr<RedesignFolderScanResult> m_results;
    QStringList m_nameFilters;
    QUrl m_folder;
    QString m_searchPattern;
    QString m_sortCriteria = QStringLiteral("title");
    QFileInfoList m_entries;
    QHash<QUrl, int> m_rowsByUrl;
    QHash<QUrl, QString> m_artwork;
    QHash<QUrl, qreal> m_progress;
    QHash<QUrl, bool> m_played;
    QSet<QUrl> m_watchRequested;
    mutable QSet<QUrl> m_artworkRequested;
    quint64 m_folderSerial = 0;
    quint64 m_serial = 0;
    quint64 m_infoSerial = 0;
    quint64 m_watchRequestSerial = 0;
    quint64 m_watchSortSerial = 0;
    bool m_watchStatesPending = false;
    bool m_watchSortRunning = false;
    bool m_watchSortNeeded = false;
    bool m_sortReversed = false;
    bool m_refreshPending = false;
};
#endif
