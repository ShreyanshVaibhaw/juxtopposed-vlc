// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "util/postprocessing_preferences.hpp"

class TestPostprocessingPreferences : public QObject
{
    Q_OBJECT
private slots:
    void effectiveQuality_data();

    void effectiveQuality();

    void updatePreservesOtherFilters();

    void duplicateFiltersAndInheritedQuality();

    void addToEmptyOrExistingChain();
};

void TestPostprocessingPreferences::effectiveQuality_data()
{
    QTest::addColumn<QString>("chain");
    QTest::addColumn<int>("expected");
    QTest::newRow("absent") << QStringLiteral("adjust:sharpen") << 0;
    QTest::newRow("native-default") << QStringLiteral("postproc") << 6;
    QTest::newRow("inline") << QStringLiteral("adjust:postproc{q=2}") << 2;
    QTest::newRow("aliases") << QStringLiteral("pp{q=3}:postprocess{q=3}") << 3;
    QTest::newRow("mixed") << QStringLiteral("postproc{q=2}:pp{q=3}:postprocess{q=2}") << -1;
    QTest::newRow("disabled") << QStringLiteral("postproc{q=0}") << 0;
    QTest::newRow("last-value") << QStringLiteral("postproc{q=1,q=4}") << 4;
    QTest::newRow("native-integer") << QStringLiteral("postproc{q=0x5}") << 5;
    QTest::newRow("native-range") << QStringLiteral("postproc{q=99}") << 6;
    QTest::newRow("unrelated-options") << QStringLiteral("adjust{note='postproc:q=2'}") << 0;
}

void TestPostprocessingPreferences::effectiveQuality()
{
    QFETCH(QString, chain);
    QFETCH(int, expected);
    QCOMPARE(vlc::postprocessing::quality(chain, 6, 6), expected);
}

void TestPostprocessingPreferences::updatePreservesOtherFilters()
{
    const QString before = R"(adjust{note="a:b,c",nested={a:b}}:postprocess{q=2,name="hb:a,\"b\\c"}:sharpen{sigma=0.7})";
    const auto updated = vlc::postprocessing::withQuality(before, 5);
    QVERIFY(updated.startsWith(R"(adjust{note="a:b,c",nested={a:b}}:postprocess{)"));
    QVERIFY(updated.endsWith(":sharpen{sigma=0.7}"));
    QCOMPARE(vlc::postprocessing::quality(updated, 6, 6), 5);
    QString beforeName, afterName;
    const auto extractName = [](const QString &chain, QString &value) {
        vlc::postprocessing::visit(chain, [&](const QByteArray &, const char *name, const config_chain_t *options) {
            if (!vlc::postprocessing::isFilter(name)) return;
            for (auto *option = options; option; option = option->p_next)
                if (option->psz_name && !strcmp(option->psz_name, "name")) value = QString::fromUtf8(option->psz_value);
        });
    };
    extractName(before, beforeName);
    extractName(updated, afterName);
    QCOMPARE(afterName, beforeName);
    QCOMPARE(vlc::postprocessing::withQuality(updated, 0),
        R"(adjust{note="a:b,c",nested={a:b}}:sharpen{sigma=0.7})");
}

void TestPostprocessingPreferences::duplicateFiltersAndInheritedQuality()
{
    const auto updated = vlc::postprocessing::withQuality("postproc{q=1,q=2}:pp:adjust:postprocess{q=4}", 3);
    QCOMPARE(updated, "postproc{q=3}:pp{q=3}:adjust:postprocess{q=3}");
    QCOMPARE(vlc::postprocessing::quality(updated, 6, 6), 3);
    QCOMPARE(vlc::postprocessing::withQuality(updated, 0), "adjust");
    QCOMPARE(vlc::postprocessing::quality("postproc", 2, 6), 2);
}

void TestPostprocessingPreferences::addToEmptyOrExistingChain()
{
    QCOMPARE(vlc::postprocessing::withQuality({}, 0), "");
    QCOMPARE(vlc::postprocessing::withQuality({}, 4), "postproc{q=4}");
    QCOMPARE(vlc::postprocessing::withQuality("adjust:sharpen", 4), "postproc{q=4}:adjust:sharpen");
    QCOMPARE(vlc::postprocessing::withQuality("adjust:sharpen", 0), "adjust:sharpen");
}

QTEST_GUILESS_MAIN(TestPostprocessingPreferences)
#include "test_postprocessing_preferences.moc"
