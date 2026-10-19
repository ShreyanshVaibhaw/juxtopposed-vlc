// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include <memory>
#include "playlist/playlist_tree_model.hpp"

using vlc::playlist::PlaylistTreeModel;

class Source : public QAbstractListModel
{
public:
    enum { Id = Qt::UserRole + 1, Ancestors, Current, Duration };
    struct Item { QString id, title; QVariantList ancestors; bool current = false; int duration = 12; };
    QList<Item> items;
    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::DisplayRole,"title"},{Id,"itemId"},{Ancestors,"ancestors"},{Current,"isCurrent"},{Duration,"duration"}};
    }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : items.size(); }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= items.size()) return {};
        const auto& item = items[index.row()];
        switch(role) {
        case Qt::DisplayRole:return item.title;
        case Id:return item.id;
        case Ancestors:return item.ancestors;
        case Current:return item.current;
        case Duration:return item.duration;
        default:return {};
        }
    }
    void append(const Item& item) { const int i=items.size();beginInsertRows({},i,i);items.append(item);endInsertRows(); }
    void reset(const QList<Item>& snapshot) { beginResetModel();items=snapshot;endResetModel(); }
    void remove(int row) { beginRemoveRows({},row,row);items.removeAt(row);endRemoveRows(); }
    void move(int from,int to) { beginMoveRows({},from,from,{},to>from?to+1:to);items.move(from,to);endMoveRows(); }
    void update(int row) { emit dataChanged(index(row,0),index(row,0)); }
    void current(int row) {
        for(int i=0;i<items.size();++i) if(items[i].current) {items[i].current=false;update(i);}
        if(row>=0) {items[row].current=true;update(row);}
    }
};

static QVariantMap group(const QString& key,bool playlist=false) {
    return {{"key",key},{"title","Group "+key},{"isPlaylist",playlist}};
}
static QStringList visibleItems(const PlaylistTreeModel& model,const Source& source) {
    QStringList result;
    for(int i=0;i<model.rowCount();++i) if(int row=model.sourceIndexAt(i);row>=0) result.append(source.items[row].id);
    return result;
}
static void populate(Source& source) {
    source.append({"0","First",{group("A")}});
    source.append({"1","Second",{group("A")}});
    source.append({"2","Loose file",{}});
    source.append({"child","Nested folder",{group("B",true)}});
    source.append({"4","Nested file",{group("B",true),group("child")}});
}

class TestPlaylistTreeModel : public QObject
{
    Q_OBJECT
private slots:
    void individualFilesStayAtRoot() {
        Source source;source.append({"0","First",{}});source.append({"1","Second",{}});
        PlaylistTreeModel model;model.setSourceModel(&source);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(),2);
        for(int i=0;i<2;++i) {
            QCOMPARE(model.sourceIndexAt(i),i);
            QCOMPARE(model.data(model.index(i,0),PlaylistTreeModel::TreeDepthRole).toInt(),0);
            QVERIFY(!model.data(model.index(i,0),PlaylistTreeModel::TreeGroupRole).toBool());
            QCOMPARE(model.data(model.index(i,0),Source::Duration).toInt(),12);
        }
        model.setTreeEnabled(false);QCOMPARE(model.rowCount(),2);QCOMPARE(source.rowCount(),2);
    }
    void nestedGroupsAndFlatMode() {
        Source source;populate(source);PlaylistTreeModel model;model.setSourceModel(&source);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(),7);
        const QList<int> indexes{-1,0,1,2,-1,3,4},depths{0,1,1,0,0,1,2};
        for(int i=0;i<7;++i) {
            QCOMPARE(model.sourceIndexAt(i),indexes[i]);
            QCOMPARE(model.data(model.index(i,0),PlaylistTreeModel::TreeDepthRole).toInt(),depths[i]);
        }
        QCOMPARE(model.data(model.index(0,0)).toString(),QString("Group A"));
        QVERIFY(!model.data(model.index(0,0),Source::Current).toBool());
        QVERIFY(!model.data(model.index(0,0),Source::Duration).isValid());
        QVERIFY(model.data(model.index(4,0),PlaylistTreeModel::TreePlaylistRole).toBool());
        QVERIFY(model.data(model.index(5,0),PlaylistTreeModel::TreeGroupRole).toBool());
        QCOMPARE(model.rowForGroupKey("child"),5);
        model.toggleExpanded(0);QCOMPARE(model.rowCount(),5);QCOMPARE(model.sourceIndexAt(1),2);
        model.setTreeEnabled(false);QCOMPARE(model.rowCount(),5);
        QCOMPARE(visibleItems(model,source),QStringList({"0","1","2","child","4"}));
        model.setTreeEnabled(true);QCOMPARE(model.rowCount(),5);
        model.setExpanded(0,true);QCOMPARE(model.rowCount(),7);QCOMPARE(source.rowCount(),5);
    }
    void currentItemRevealAndMetadataDoNotResetTree() {
        Source source;populate(source);source.current(4);PlaylistTreeModel model;model.setSourceModel(&source);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.toggleExpanded(model.rowForGroupKey("B"));QCOMPARE(model.rowCount(),5);
        QVERIFY(model.data(model.index(4,0),PlaylistTreeModel::ContainsCurrentRole).toBool());
        QSignalSpy resets(&model,&QAbstractItemModel::modelReset);
        source.items[4].title="Updated title";source.update(4);QCoreApplication::processEvents();
        QCOMPARE(resets.count(),0);QCOMPARE(model.rowForSourceIndex(4),-1);
        source.current(0);source.current(4);
        QTRY_COMPARE(model.rowCount(),7);
        QVERIFY(model.data(model.index(5,0),PlaylistTreeModel::ContainsCurrentRole).toBool());
        QCOMPARE(model.data(model.index(6,0)).toString(),QString("Updated title"));
        model.toggleExpanded(model.rowForGroupKey("B"));QCOMPARE(model.rowForSourceIndex(4,true),6);
        QCOMPARE(model.sourceIndexAt(6),4);
    }
    void repeatedRootsPreserveActualQueueOrder() {
        Source source;source.append({"0","A first",{group("A")}});source.append({"1","B",{group("B")}});source.append({"2","A last",{group("A")}});
        PlaylistTreeModel model;model.setSourceModel(&source);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.rowCount(),6);QCOMPARE(visibleItems(model,source),QStringList({"0","1","2"}));
        model.toggleExpanded(0);QCOMPARE(model.rowCount(),4);QCOMPARE(visibleItems(model,source),QStringList({"1"}));
        source.move(2,0);QTRY_COMPARE(model.rowCount(),3);
        model.setExpanded(0,true);QCOMPARE(model.rowCount(),5);
        QCOMPARE(visibleItems(model,source),QStringList({"2","0","1"}));
        QCOMPARE(source.items[0].id,QString("2"));
    }
    void insertionBoundariesUseTheWholeGroup() {
        Source source;populate(source);PlaylistTreeModel model;model.setSourceModel(&source);
        QCOMPARE(model.insertionIndexAt(0,false),0);QCOMPARE(model.insertionIndexAt(0,true),2);
        QCOMPARE(model.insertionIndexAt(1,true),1);
        QCOMPARE(model.insertionIndexAt(5,false),3);QCOMPARE(model.insertionIndexAt(5,true),5);
        QCOMPARE(model.rowForInsertion(0),0);QCOMPARE(model.rowForInsertion(2),3);QCOMPARE(model.rowForInsertion(5),7);
        model.toggleExpanded(4);QCOMPARE(model.insertionIndexAt(4,true),5);QCOMPARE(model.rowForInsertion(5),5);
        QCOMPARE(model.insertionIndexAt(-1,false),-1);QCOMPARE(model.sourceIndexAt(99),-1);
    }
    void structuralChangesCoalesceAndMetadataKeepsDelegates() {
        Source source;PlaylistTreeModel model;model.setSourceModel(&source);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&model,&QAbstractItemModel::modelReset);
        for(int i=0;i<100;++i) source.append({QString::number(i),"Item",{group("A")}});
        QTRY_COMPARE(model.rowCount(),101);QCOMPARE(resets.count(),1);
        resets.clear();source.items[0].duration=35;source.update(0);QCoreApplication::processEvents();
        QCOMPARE(resets.count(),0);QCOMPARE(model.data(model.index(1,0),Source::Duration).toInt(),35);
        source.items[0].id="replacement";source.update(0);QTRY_COMPARE(resets.count(),1);
        QCOMPARE(model.data(model.index(1,0),PlaylistTreeModel::TreeKeyRole).toString(),QString("replacement"));
        while(source.rowCount()) source.remove(0);
        QTRY_COMPARE(model.rowCount(),0);
        source.append({"next","Next",{group("A")}});QTRY_COMPARE(model.rowCount(),2);
    }
    void sourceDestructionAndReplacement() {
        auto *source=new Source;populate(*source);PlaylistTreeModel model;model.setSourceModel(source);
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        delete source;QCOMPARE(model.rowCount(),0);QVERIFY(!model.sourceModel());
        Source next;next.append({"new","Next",{group("next")}});model.setSourceModel(&next);QCOMPARE(model.rowCount(),2);
        model.setSourceModel(nullptr);QCOMPARE(model.rowCount(),0);
    }
    void lazyReattachmentKeepsNestedCollapsedGroups() {
        PlaylistTreeModel model;
        QAbstractItemModelTester tester(&model,QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto source=std::make_unique<Source>();populate(*source);source->current(4);
        const auto snapshot=source->items;
        model.setSourceModel(source.get());
        model.setExpanded(model.rowForGroupKey("child"),false);
        model.setExpanded(model.rowForGroupKey("B"),false);
        model.setExpanded(model.rowForGroupKey("A"),false);
        QCOMPARE(model.rowCount(),3);
        source.reset();QVERIFY(!model.sourceModel());QCOMPARE(model.rowCount(),0);

        Source reopened;model.setSourceModel(&reopened);
        model.setTreeEnabled(false);model.setTreeEnabled(true);
        QCOMPARE(model.rowCount(),0);
        // A new PlaylistListModel delivers its initial snapshot asynchronously.
        reopened.reset(snapshot);QTRY_COMPARE(model.rowCount(),3);
        QVERIFY(!model.expandedAt(model.rowForGroupKey("A")));
        QVERIFY(!model.expandedAt(model.rowForGroupKey("B")));
        model.setExpanded(model.rowForGroupKey("B"),true);
        QVERIFY(!model.expandedAt(model.rowForGroupKey("child")));
        QCOMPARE(model.rowForSourceIndex(4),-1);
        model.setExpanded(model.rowForGroupKey("child"),true);
        QVERIFY(model.rowForSourceIndex(4)>=0);
        QCOMPARE(reopened.rowCount(),snapshot.size());
        for(int i=0;i<snapshot.size();++i) {
            QCOMPARE(reopened.items[i].id,snapshot[i].id);
            QCOMPARE(reopened.items[i].current,snapshot[i].current);
        }
    }
    void changedCurrentIsRevealedAfterReattachment() {
        Source source;populate(source);source.current(0);
        PlaylistTreeModel model;model.setSourceModel(&source);
        model.setExpanded(model.rowForGroupKey("B"),false);
        model.setExpanded(model.rowForGroupKey("A"),false);
        model.setSourceModel(nullptr);
        QSignalSpy resets(&model,&QAbstractItemModel::modelReset);
        source.current(4);source.items[0].title="Renamed while hidden";source.update(0);
        QCoreApplication::processEvents();QCOMPARE(resets.count(),0);QCOMPARE(model.rowCount(),0);
        Source reopened;model.setSourceModel(&reopened);reopened.reset(source.items);
        QTRY_VERIFY(model.rowForSourceIndex(4)>=0);
        QVERIFY(model.expandedAt(model.rowForGroupKey("B")));
        QVERIFY(!model.expandedAt(model.rowForGroupKey("A")));
    }
    void initializedEmptyQueueClearsOldCollapsedGroups() {
        Source source;populate(source);PlaylistTreeModel model;model.setSourceModel(&source);
        model.setExpanded(model.rowForGroupKey("A"),false);
        model.setSourceModel(nullptr);
        Source empty;model.setSourceModel(&empty);
        QSignalSpy resets(&model,&QAbstractItemModel::modelReset);
        empty.reset({});QTRY_COMPARE(resets.count(),1);
        model.setSourceModel(nullptr);
        Source replaced;populate(replaced);model.setSourceModel(&replaced);
        QVERIFY(model.expandedAt(model.rowForGroupKey("A")));

        model.setExpanded(model.rowForGroupKey("B"),false);
        model.setSourceModel(nullptr);
        Source remaining;remaining.append({"other","Other",{group("A")}});
        model.setSourceModel(&remaining);
        remaining.append({"new","New",{group("B")}});
        QTRY_VERIFY(model.rowForGroupKey("B")>=0);
        QVERIFY(model.expandedAt(model.rowForGroupKey("B")));
    }
};
QTEST_GUILESS_MAIN(TestPlaylistTreeModel)
#include "test_playlist_tree_model.moc"
