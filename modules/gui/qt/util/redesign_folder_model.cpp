// SPDX-License-Identifier: GPL-2.0-or-later
#include "redesign_folder_model.hpp"
#include <QTimer>
#include <QDir>
#include <QDateTime>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
QUrl normalizeFolderUrl(const QUrl& folder)
{
    if (folder.scheme().isEmpty()) {
        QString path = folder.path();
        if (!folder.host().isEmpty()) path.prepend("//" + folder.host());
        if (QDir::isAbsolutePath(path)) return QUrl::fromLocalFile(path);
    }
#ifdef Q_OS_WIN
    else if (folder.scheme().size() == 1) {
        const QString path = folder.scheme() + ":" + folder.path();
        if (QDir::isAbsolutePath(path)) return QUrl::fromLocalFile(path);
    }
#endif
    return folder;
}

void sortEntries(QFileInfoList& entries, const QString& sort, bool reversed,
                 const QHash<QUrl, bool>& played)
{
    std::sort(entries.begin(), entries.end(), [&sort, reversed, &played](const QFileInfo& a, const QFileInfo& b) {
        if (a.isDir() != b.isDir()) return a.isDir();
        int primary;
        if (sort == "played") {
            const bool av = !a.isDir() && played.value(QUrl::fromLocalFile(a.absoluteFilePath()), false);
            const bool bv = !b.isDir() && played.value(QUrl::fromLocalFile(b.absoluteFilePath()), false);
            if (av != bv) return reversed ? av : !av;
            // Watch groups keep a predictable title order in both directions.
            primary = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive);
            if (primary) return primary < 0;
        } else {
            if (sort == "insertion") {
                const auto av = a.lastModified();
                const auto bv = b.lastModified();
                primary = av < bv ? -1 : av > bv ? 1 : 0;
            } else if (sort == "uri") {
                primary = QUrl::fromLocalFile(a.absoluteFilePath()).toEncoded().compare(
                    QUrl::fromLocalFile(b.absoluteFilePath()).toEncoded());
            } else primary = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive);
            if (primary) return reversed ? primary > 0 : primary < 0;
        }
        return a.fileName() < b.fileName();
    });
}
}

RedesignFolderModel::RedesignFolderModel(QObject* parent)
    : QAbstractListModel(parent)
{
    qRegisterMetaType<QFileInfoList>();
    m_results = std::shared_ptr<RedesignFolderScanResult>(new RedesignFolderScanResult,
        [](RedesignFolderScanResult* result) { result->deleteLater(); });
    connect(m_results.get(), &RedesignFolderScanResult::ready, this,
        [this](quint64 serial, const QFileInfoList& entries) {
            if (serial == m_serial)
                replaceEntries(entries);
        });
    connect(m_results.get(), &RedesignFolderScanResult::infoReady, this, &RedesignFolderModel::infoReady);
    connect(m_results.get(), &RedesignFolderScanResult::watchSorted, this,
        [this](quint64 serial, const QFileInfoList& entries) {
            m_watchSortRunning = false;
            if (serial == m_watchSortSerial && m_sortCriteria == "played" && !m_watchStatesPending)
                applyWatchOrder(entries);
            if (m_watchSortNeeded) QTimer::singleShot(0, this, &RedesignFolderModel::startWatchSort);
        });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
        &RedesignFolderModel::scheduleRefresh);
}

void RedesignFolderModel::setFolder(const QUrl& folder)
{
    const auto normalized = normalizeFolderUrl(folder);
    if (m_folder == normalized)
        return;
    m_folder = normalized;
    ++m_folderSerial;
    m_artwork.clear();
    m_progress.clear();
    m_played.clear();
    m_watchRequested.clear();
    m_artworkRequested.clear();
    const auto watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (m_folder.isLocalFile() && QFileInfo(m_folder.toLocalFile()).isDir())
        m_watcher.addPath(m_folder.toLocalFile());
    replaceEntries({});
    scheduleRefresh();
    emit folderChanged();
}

void RedesignFolderModel::refresh()
{
    ++m_folderSerial;
    m_artwork.clear();
    m_progress.clear();
    m_played.clear();
    m_watchRequested.clear();
    m_artworkRequested.clear();
    scheduleRefresh();
}

quint64 RedesignFolderModel::requestInfo(const QUrl& folder)
{
    const auto serial = ++m_infoSerial;
    const auto results = m_results;
    const auto url = normalizeFolderUrl(folder);
    QThreadPool::globalInstance()->start([results, url, serial] {
        QVariantMap info {{"success", false}, {"url", url.toString(QUrl::FullyEncoded)},
            {"location", url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toDisplayString()}};
        if (url.isLocalFile()) {
            const QFileInfo directory(url.toLocalFile());
            if (directory.isDir() && directory.isReadable()) {
                quint64 size = 0;
                int files = 0, folders = 0;
                const auto entries = QDir(directory.absoluteFilePath()).entryInfoList(
                    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
                for (const auto& entry : entries) {
                    if (entry.isDir()) ++folders;
                    else {
                        ++files;
                        size += quint64(std::max(qint64(0), entry.size()));
                    }
                }
                info = {{"success", true}, {"url", url.toString(QUrl::FullyEncoded)},
                    {"name", directory.fileName().isEmpty() ? directory.absoluteFilePath() : directory.fileName()},
                    {"location", QDir::toNativeSeparators(directory.absoluteFilePath())},
                    {"files", files}, {"folders", folders}, {"size", QVariant::fromValue(size)},
                    {"created", directory.birthTime().isValid() ? QVariant(directory.birthTime().toMSecsSinceEpoch()) : QVariant()},
                    {"modified", directory.lastModified().isValid() ? QVariant(directory.lastModified().toMSecsSinceEpoch()) : QVariant()},
                    {"readable", directory.isReadable()}, {"writable", directory.isWritable()},
                    {"symbolicLink", directory.isSymLink()}};
            }
        }
        emit results->infoReady(serial, info);
    });
    return serial;
}

void RedesignFolderModel::setNameFilters(const QStringList& filters)
{
    if (filters == nameFilters())
        return;
    m_nameFilters = filters;
    scheduleRefresh();
    emit nameFiltersChanged();
}

void RedesignFolderModel::setSortReversed(bool reversed)
{
    if (m_sortReversed == reversed)
        return;
    m_sortReversed = reversed;
    if (m_sortCriteria == "played") scheduleWatchSort();
    else scheduleRefresh();
    emit sortReversedChanged();
}

void RedesignFolderModel::setSortCriteria(const QString& criteria)
{
    if (criteria != "title" && criteria != "insertion" && criteria != "uri" && criteria != "played")
        return;
    if (m_sortCriteria == criteria) return;
    m_sortCriteria = criteria;
    if (criteria == "played") requestWatchStates();
    else scheduleRefresh();
    emit sortCriteriaChanged();
}

int RedesignFolderModel::directoryCount() const
{
    return std::count_if(m_entries.cbegin(), m_entries.cend(),
                         [](const QFileInfo& file) { return file.isDir(); });
}

void RedesignFolderModel::setSearchPattern(const QString& pattern)
{
    if (m_searchPattern == pattern)
        return;
    m_searchPattern = pattern;
    scheduleRefresh();
    emit searchPatternChanged();
}

int RedesignFolderModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QHash<int, QByteArray> RedesignFolderModel::roleNames() const
{
    return {{FileName, "fileName"}, {FileUrl, "fileUrl"}, {FileIsDir, "fileIsDir"},
            {Thumbnail, "thumbnail"}, {Progress, "progress"}};
}

QVariant RedesignFolderModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= count())
        return {};
    const auto& file = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case FileName: return file.fileName();
    case FileUrl: return QUrl::fromLocalFile(file.absoluteFilePath());
    case FileIsDir: return file.isDir();
    case Thumbnail:
    case Progress: {
        if (file.isDir()) return role == Thumbnail ? QVariant(QString {}) : QVariant(0.0);
        const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
        if (!m_artworkRequested.contains(url)) {
            m_artworkRequested.insert(url);
            auto self = const_cast<RedesignFolderModel*>(this);
            QTimer::singleShot(0, self, [self, url, serial = m_folderSerial] {
                if (serial == self->m_folderSerial)
                    emit self->artworkRequested(url);
            });
        }
        return role == Thumbnail ? QVariant(m_artwork.value(url)) : QVariant(m_progress.value(url, 0.0));
    }
    default: return {};
    }
}

void RedesignFolderModel::setArtwork(const QUrl& url, const QString& artwork)
{
    // Results are tied to the requested file, never to a row that may have moved.
    if (!m_artworkRequested.contains(url) || m_artwork.value(url) == artwork)
        return;
    m_artwork.insert(url, artwork);
    const auto row = m_rowsByUrl.value(url, -1);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {Thumbnail});
}

void RedesignFolderModel::setProgress(const QUrl& url, qreal progress)
{
    if (!m_artworkRequested.contains(url))
        return;
    const qreal bounded = std::isfinite(progress) ? std::clamp(progress, qreal(0), qreal(1)) : 0;
    if (m_progress.value(url, 0.0) == bounded)
        return;
    m_progress.insert(url, bounded);
    const auto row = m_rowsByUrl.value(url, -1);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {Progress});
}

void RedesignFolderModel::setPlayed(const QUrl& url, bool played)
{
    if (!m_watchRequested.contains(url) && !m_artworkRequested.contains(url)) return;
    const bool previous = m_played.value(url, false);
    m_played.insert(url, played);
    if (previous != played && m_rowsByUrl.contains(url) && m_sortCriteria == "played" && !m_watchStatesPending)
        scheduleWatchSort();
}

void RedesignFolderModel::setWatchStatesPending(bool pending)
{
    if (m_watchStatesPending == pending) return;
    m_watchStatesPending = pending;
    emit watchStatesPendingChanged();
}

void RedesignFolderModel::requestWatchStates()
{
    ++m_watchSortSerial;
    const auto serial = ++m_watchRequestSerial;
    QList<QUrl> urls;
    for (const auto& file : std::as_const(m_entries)) {
        if (file.isDir()) continue;
        const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
        urls.append(url);
        m_watchRequested.insert(url);
    }
    setWatchStatesPending(!urls.isEmpty());
    if (urls.isEmpty()) scheduleWatchSort();
    else emit watchStatesRequested(urls, serial);
}

void RedesignFolderModel::finishWatchStates(quint64 serial)
{
    if (serial != m_watchRequestSerial || m_sortCriteria != "played" || !m_watchStatesPending) return;
    setWatchStatesPending(false);
    scheduleWatchSort();
}

void RedesignFolderModel::scheduleWatchSort()
{
    ++m_watchSortSerial;
    if (m_watchSortNeeded) return;
    m_watchSortNeeded = true;
    QTimer::singleShot(0, this, &RedesignFolderModel::startWatchSort);
}

void RedesignFolderModel::startWatchSort()
{
    if (m_watchSortRunning || !m_watchSortNeeded || m_watchStatesPending || m_sortCriteria != "played") return;
    m_watchSortNeeded = false;
    m_watchSortRunning = true;
    const auto results = m_results;
    const auto serial = m_watchSortSerial;
    const auto played = m_played;
    const auto reversed = m_sortReversed;
    auto entries = m_entries;
    QThreadPool::globalInstance()->start([results, serial, played, reversed, entries = std::move(entries)]() mutable {
        sortEntries(entries, QStringLiteral("played"), reversed, played);
        emit results->watchSorted(serial, entries);
    });
}

void RedesignFolderModel::applyWatchOrder(const QFileInfoList& entries)
{
    if (entries.size() != m_entries.size()) return;
    bool changed = false;
    for (qsizetype row = 0; row < entries.size(); ++row)
        changed |= entries[row].absoluteFilePath() != m_entries[row].absoluteFilePath();
    if (!changed) return;
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    const auto persistent = persistentIndexList();
    QList<QUrl> urls;
    for (const auto& index : persistent)
        urls.append(QUrl::fromLocalFile(m_entries[index.row()].absoluteFilePath()));
    m_entries = entries;
    rebuildRows();
    QModelIndexList updated;
    for (const auto& url : urls) updated.append(index(m_rowsByUrl.value(url, -1)));
    changePersistentIndexList(persistent, updated);
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
}

QVariant RedesignFolderModel::get(int row, const QString& role) const
{
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (QString::fromLatin1(it.value()) == role)
            return data(index(row, 0), it.key());
    return {};
}

void RedesignFolderModel::scheduleRefresh()
{
    ++m_serial;
    ++m_watchRequestSerial;
    ++m_watchSortSerial;
    m_watchSortNeeded = false;
    setWatchStatesPending(false);
    emit watchStatesCancelled();
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        scan();
    });
}

void RedesignFolderModel::scan()
{
    const auto results = m_results;
    const auto folder = m_folder;
    const auto filters = m_nameFilters;
    const auto serial = m_serial;
    const auto reversed = m_sortReversed;
    const auto sort = m_sortCriteria;
    const auto search = m_searchPattern;
    const auto played = m_played;
    QThreadPool::globalInstance()->start([results, folder, filters, serial, reversed, sort, search, played] {
        QFileInfoList entries;
        if (folder.isLocalFile()) {
            QDir directory(folder.toLocalFile());
            entries = directory.entryInfoList(filters,
                QDir::AllEntries | QDir::AllDirs | QDir::Readable | QDir::NoDotAndDotDot,
                QDir::NoSort);
        }
        if (!search.isEmpty())
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&search](const QFileInfo& file) {
                return !file.fileName().contains(search, Qt::CaseInsensitive);
            }), entries.end());
        sortEntries(entries, sort, reversed, played);
        emit results->ready(serial, entries);
    });
}

void RedesignFolderModel::replaceEntries(const QFileInfoList& entries)
{
    const int oldCount = count();
    const int oldDirectoryCount = directoryCount();
    beginResetModel();
    m_entries = entries;
    rebuildRows();
    endResetModel();
    if (oldCount != count() || oldDirectoryCount != directoryCount())
        emit countChanged();
    if (m_sortCriteria == "played") requestWatchStates();
}

void RedesignFolderModel::rebuildRows()
{
    m_rowsByUrl.clear();
    for (int row = 0; row < m_entries.size(); ++row)
        m_rowsByUrl.insert(QUrl::fromLocalFile(m_entries[row].absoluteFilePath()), row);
}
