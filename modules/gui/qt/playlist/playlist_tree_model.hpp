// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_PLAYLIST_TREE_MODEL_HPP
#define VLC_QT_PLAYLIST_TREE_MODEL_HPP

#include <QAbstractListModel>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantList>

namespace vlc::playlist {

// A visible tree projection of the flat play queue. Source indexes, order and
// ownership remain with the playlist; virtual group rows are never queue items.
class PlaylistTreeModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* sourceModel READ sourceModel WRITE setSourceModel NOTIFY sourceModelChanged FINAL)
    Q_PROPERTY(bool treeEnabled READ treeEnabled WRITE setTreeEnabled NOTIFY treeEnabledChanged FINAL)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)
public:
    enum Roles {
        QueueIndexRole = Qt::UserRole + 1024,
        TreeDepthRole, TreeGroupRole, TreeExpandedRole, TreeKeyRole,
        TreePlaylistRole, ContainsCurrentRole,
    };
    Q_ENUM(Roles)
    explicit PlaylistTreeModel(QObject* parent = nullptr);
    QAbstractItemModel* sourceModel() const { return m_source; }
    void setSourceModel(QAbstractItemModel* source);
    bool treeEnabled() const { return m_treeEnabled; }
    void setTreeEnabled(bool enabled);
    int rowCount(const QModelIndex& parent = {}) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Q_INVOKABLE int sourceIndexAt(int row) const;
    Q_INVOKABLE int rowForSourceIndex(int sourceIndex, bool reveal = false);
    Q_INVOKABLE int rowForGroupKey(const QString& key) const;
    Q_INVOKABLE void setExpanded(int row, bool expanded);
    Q_INVOKABLE void toggleExpanded(int row);
    Q_INVOKABLE bool expandedAt(int row) const;
    Q_INVOKABLE int insertionIndexAt(int row, bool after) const;
    Q_INVOKABLE int rowForInsertion(int sourceIndex) const;
signals:
    void sourceModelChanged();
    void treeEnabledChanged();
    void countChanged();
private:
    struct Row {
        QPersistentModelIndex source;
        QString key, title;
        int depth = 0, first = 0, last = 0;
        bool group = false, expanded = false, playlist = false;
    };
    void rebuild();
    void sourceDataChanged(const QModelIndex& first, const QModelIndex& last);
    int role(const QByteArray& name) const;
    bool revealAncestors(const QModelIndex& sourceIndex);
    QPointer<QAbstractItemModel> m_source;
    QList<QMetaObject::Connection> m_connections;
    QHash<int, QByteArray> m_roles;
    QList<Row> m_rows;
    QStringList m_keys;
    QList<QVariantList> m_paths;
    QSet<QString> m_collapsed;
    QPersistentModelIndex m_current;
    QString m_currentKey;
    QTimer m_rebuildTimer;
    bool m_sourceReady = false;
    bool m_treeEnabled = true;
};
}
#endif
