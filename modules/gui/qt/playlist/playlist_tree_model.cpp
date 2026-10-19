// SPDX-License-Identifier: GPL-2.0-or-later
#include "playlist_tree_model.hpp"
#include <algorithm>

namespace vlc::playlist {
PlaylistTreeModel::PlaylistTreeModel(QObject* parent) : QAbstractListModel(parent)
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(0);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &PlaylistTreeModel::rebuild);
}

int PlaylistTreeModel::role(const QByteArray& name) const
{
    return m_roles.key(name, -1);
}

void PlaylistTreeModel::setSourceModel(QAbstractItemModel* source)
{
    if (m_source == source) return;
    for (const auto& connection : m_connections) disconnect(connection);
    m_connections.clear();
    m_source = source;
    m_roles = source ? source->roleNames() : QHash<int, QByteArray>{};
    // A lazy queue view recreates its source model when it opens. Keep only
    // expansion state until that source has delivered its initial snapshot.
    m_sourceReady = source && source->rowCount() > 0;
    m_current = QModelIndex{};
    if (source) {
        const auto changed = [this] { m_sourceReady = true; m_rebuildTimer.start(); };
        m_connections = {
            connect(source, &QAbstractItemModel::modelReset, this, changed),
            connect(source, &QAbstractItemModel::rowsInserted, this, changed),
            connect(source, &QAbstractItemModel::rowsRemoved, this, changed),
            connect(source, &QAbstractItemModel::rowsMoved, this, changed),
            connect(source, &QAbstractItemModel::layoutChanged, this, changed),
            connect(source, &QAbstractItemModel::dataChanged, this, &PlaylistTreeModel::sourceDataChanged),
            connect(source, &QObject::destroyed, this, [this] {
                m_source = nullptr;
                m_roles.clear();
                m_sourceReady = false;
                rebuild();
                emit sourceModelChanged();
            }),
        };
    }
    rebuild();
    emit sourceModelChanged();
}

void PlaylistTreeModel::setTreeEnabled(bool enabled)
{
    if (m_treeEnabled == enabled) return;
    m_treeEnabled = enabled;
    rebuild();
    emit treeEnabledChanged();
}

int PlaylistTreeModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> PlaylistTreeModel::roleNames() const
{
    auto result = m_roles;
    result.insert(QueueIndexRole, "queueIndex");
    result.insert(TreeDepthRole, "treeDepth");
    result.insert(TreeGroupRole, "treeGroup");
    result.insert(TreeExpandedRole, "treeExpanded");
    result.insert(TreeKeyRole, "treeKey");
    result.insert(TreePlaylistRole, "treePlaylist");
    result.insert(ContainsCurrentRole, "containsCurrent");
    return result;
}

QVariant PlaylistTreeModel::data(const QModelIndex& index, int requested) const
{
    if (!index.isValid() || index.model() != this || index.row() >= m_rows.size()) return {};
    const auto& row = m_rows[index.row()];
    switch (requested) {
    case QueueIndexRole: return row.source.isValid() ? row.source.row() : -1;
    case TreeDepthRole: return row.depth;
    case TreeGroupRole: return row.group;
    case TreeExpandedRole: return row.expanded;
    case TreeKeyRole: return row.key;
    case TreePlaylistRole: return row.playlist;
    case ContainsCurrentRole:
        return row.group && m_current.isValid()
            && m_current.row() >= row.first && m_current.row() <= row.last;
    default:
        if (row.source.isValid()) return row.source.data(requested);
        if (requested == role("title")) return row.title;
        if (requested == role("isCurrent")) return false;
        return {};
    }
}

bool PlaylistTreeModel::revealAncestors(const QModelIndex& sourceIndex)
{
    bool changed = false;
    for (const auto& ancestor : sourceIndex.data(role("ancestors")).toList())
        changed |= m_collapsed.remove(ancestor.toMap().value("key").toString());
    return changed;
}

void PlaylistTreeModel::rebuild()
{
    m_rebuildTimer.stop();
    beginResetModel();
    m_rows.clear(); m_keys.clear(); m_paths.clear();
    QHash<QString, QVariantMap> groups;
    const int size = m_source ? m_source->rowCount() : 0;
    m_keys.reserve(size); m_paths.reserve(size);
    m_current = QModelIndex{};
    for (int i = 0; i < size; ++i) {
        const auto index = m_source->index(i, 0);
        m_keys.append(index.data(role("itemId")).toString());
        m_paths.append(index.data(role("ancestors")).toList());
        for (const auto& ancestor : m_paths.last()) {
            const auto node = ancestor.toMap();
            groups.insert(node.value("key").toString(), node);
        }
        if (index.data(role("isCurrent")).toBool()) m_current = index;
    }
    if (m_sourceReady) {
        m_collapsed.intersect(QSet<QString>(groups.keyBegin(), groups.keyEnd()));
        const auto currentKey = m_current.isValid() ? m_keys[m_current.row()] : QString();
        if (currentKey != m_currentKey && m_current.isValid()) revealAncestors(m_current);
        m_currentKey = currentKey;
    }

    QStringList path;
    QList<int> headers;
    const auto close = [&](int keep, int end) {
        while (path.size() > keep) {
            if (headers.last() >= 0) m_rows[headers.last()].last = end;
            headers.removeLast(); path.removeLast();
        }
    };
    for (int i = 0; i < size; ++i) {
        const auto source = m_source->index(i, 0);
        const auto ancestors = m_treeEnabled ? m_paths[i] : QVariantList{};
        int common = 0;
        while (common < path.size() && common < ancestors.size()
               && path[common] == ancestors[common].toMap().value("key").toString()) ++common;
        close(common, i - 1);
        bool hidden = std::any_of(path.cbegin(), path.cend(), [this](const auto& key) { return m_collapsed.contains(key); });
        for (int depth = common; depth < ancestors.size(); ++depth) {
            const auto node = ancestors[depth].toMap();
            const auto key = node.value("key").toString();
            const bool expanded = !m_collapsed.contains(key);
            headers.append(hidden ? -1 : m_rows.size());
            if (!hidden) m_rows.append(Row{{}, key, node.value("title").toString(), depth, i, i,
                                           true, expanded, node.value("isPlaylist").toBool()});
            path.append(key);
            hidden |= !expanded;
        }
        const auto key = m_keys[i];
        const bool group = m_treeEnabled && groups.contains(key);
        const bool expanded = !m_collapsed.contains(key);
        const int header = hidden ? -1 : m_rows.size();
        if (!hidden) m_rows.append(Row{source, key, {}, int(path.size()), i, i, group, expanded,
                                      groups.value(key).value("isPlaylist").toBool()});
        if (group) { path.append(key); headers.append(header); }
    }
    close(0, size - 1);
    if (!m_source) {
        m_rows.squeeze(); m_keys.squeeze(); m_paths.squeeze();
    }
    endResetModel();
    emit countChanged();
}

void PlaylistTreeModel::sourceDataChanged(const QModelIndex& first, const QModelIndex& last)
{
    if (!m_source || !first.isValid() || !last.isValid()) return;
    for (int i = first.row(); i <= last.row(); ++i) {
        const auto index = m_source->index(i, 0);
        if (i >= m_keys.size() || m_keys[i] != index.data(role("itemId")).toString()
            || m_paths[i] != index.data(role("ancestors")).toList()) {
            m_rebuildTimer.start(); return;
        }
        if (index.data(role("isCurrent")).toBool()) {
            const bool newCurrent = m_currentKey != m_keys[i];
            m_current = index;
            m_currentKey = m_keys[i];
            if (newCurrent && revealAncestors(index)) { m_rebuildTimer.start(); return; }
        } else if (m_current == index) { m_current = QModelIndex{}; m_currentKey.clear(); }
    }
    if (!m_rows.isEmpty()) emit dataChanged(index(0, 0), index(m_rows.size() - 1, 0));
}

int PlaylistTreeModel::sourceIndexAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows[row].source.isValid() ? m_rows[row].source.row() : -1;
}

int PlaylistTreeModel::rowForSourceIndex(int sourceIndex, bool reveal)
{
    if (!m_source || sourceIndex < 0 || sourceIndex >= m_source->rowCount()) return -1;
    if (reveal && revealAncestors(m_source->index(sourceIndex, 0))) rebuild();
    for (int i = 0; i < m_rows.size(); ++i)
        if (sourceIndexAt(i) == sourceIndex) return i;
    return -1;
}

int PlaylistTreeModel::rowForGroupKey(const QString& key) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].group && m_rows[i].key == key) return i;
    return -1;
}

void PlaylistTreeModel::setExpanded(int row, bool expanded)
{
    if (row < 0 || row >= m_rows.size() || !m_rows[row].group) return;
    const auto key = m_rows[row].key;
    if (expanded) m_collapsed.remove(key); else m_collapsed.insert(key);
    rebuild();
}

void PlaylistTreeModel::toggleExpanded(int row)
{
    if (row >= 0 && row < m_rows.size()) setExpanded(row, !m_rows[row].expanded);
}

bool PlaylistTreeModel::expandedAt(int row) const
{
    return row >= 0 && row < m_rows.size() && m_rows[row].group && m_rows[row].expanded;
}

int PlaylistTreeModel::insertionIndexAt(int row, bool after) const
{
    if (row < 0 || row >= m_rows.size()) return -1;
    return after ? m_rows[row].last + 1 : m_rows[row].first;
}

int PlaylistTreeModel::rowForInsertion(int sourceIndex) const
{
    for (int i = 0; i < m_rows.size(); ++i) if (m_rows[i].first >= sourceIndex) return i;
    return m_rows.size();
}
}
