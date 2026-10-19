// SPDX-License-Identifier: GPL-2.0-or-later
#include "util/gettext_translator.hpp"
#include <QCoreApplication>
#include <QMap>
#include <QTemporaryFile>
#include <QTest>
#include <QtEndian>

class TestGettextTranslator : public QObject
{
    Q_OBJECT
    static QByteArray catalog(const QByteArray& rule = "n != 1", unsigned forms = 2, bool bigEndian = false)
    {
        QMap<QByteArray, QByteArray> messages;
        messages.insert({}, "Language: fr\nContent-Type: text/plain; charset=UTF-8\nPlural-Forms: nplurals=" + QByteArray::number(forms) + "; plural=" + rule + ";\n");
        messages.insert("Open file", QStringLiteral("Ouvrir un fichier").toUtf8());
        messages.insert("Unicode", QStringLiteral("Français العربية 日本語").toUtf8());
        messages.insert("Recording", QStringLiteral("Enregistrement en cours").toUtf8());
        messages.insert("Settings\004Recording", QStringLiteral("Enregistrement").toUtf8());
        messages.insert("Settings\004Only here", QStringLiteral("Uniquement ici").toUtf8());
        QByteArray translated;
        for (unsigned i = 0; i < forms; ++i) {
            if (i) translated.append('\0');
            translated.append("form" + QByteArray::number(i) + " %n");
        }
        QByteArray plural("%n item"); plural.append('\0'); plural.append("%n items");
        messages.insert(plural, translated);
        messages.insert(QByteArray("Settings\004") + plural, QByteArray(translated).replace("form", "settings"));
        const unsigned count = unsigned(messages.size());
        QByteArray result(int(28 + count * 16), '\0');
        const auto word = [&](unsigned offset, quint32 value) {
            if (bigEndian) qToBigEndian(value, result.data() + offset);
            else qToLittleEndian(value, result.data() + offset);
        };
        word(0, 0x950412de); word(8, count); word(12, 28); word(16, 28 + count * 8);
        unsigned i = 0;
        for (auto it = messages.cbegin(); it != messages.cend(); ++it, ++i) {
            word(28 + i * 8, unsigned(it.key().size())); word(32 + i * 8, unsigned(result.size()));
            result.append(it.key()); result.append('\0');
            word(28 + count * 8 + i * 8, unsigned(it.value().size()));
            word(32 + count * 8 + i * 8, unsigned(result.size()));
            result.append(it.value()); result.append('\0');
        }
        return result;
    }
    static bool load(GettextTranslator& translator, const QByteArray& bytes)
    {
        QTemporaryFile file;
        if (!file.open() || file.write(bytes) != bytes.size() || !file.flush()) return false;
        return translator.loadCatalog(file.fileName());
    }
private slots:
    void singularUnicodeAndMissingMessages_data()
    {
        QTest::addColumn<bool>("bigEndian");
        QTest::newRow("little endian") << false;
        QTest::newRow("big endian") << true;
    }
    void singularUnicodeAndMissingMessages()
    {
        QFETCH(bool, bigEndian);
        GettextTranslator translator;
        QVERIFY(translator.isEmpty());
        QVERIFY(load(translator, catalog("n != 1", 2, bigEndian)));
        QVERIFY(!translator.isEmpty());
        QCOMPARE(translator.catalogLanguage(), QStringLiteral("fr"));
        QCOMPARE(translator.translate("Any QML or C++ context", "Open file"), QStringLiteral("Ouvrir un fichier"));
        QCOMPARE(translator.translate("", "Unicode"), QStringLiteral("Français العربية 日本語"));
        QVERIFY(translator.translate("", "Missing").isNull());
        QVERIFY(translator.translate("", "").isNull());
        QVERIFY(translator.translate("", nullptr).isNull());
    }
    void plurals_data()
    {
        QTest::addColumn<QByteArray>("rule"); QTest::addColumn<unsigned>("forms");
        QTest::addColumn<QList<int>>("counts"); QTest::addColumn<QList<int>>("expected");
        QTest::newRow("English") << QByteArray("n != 1") << 2u << QList<int>{0,1,2} << QList<int>{1,0,1};
        QTest::newRow("French") << QByteArray("n > 1") << 2u << QList<int>{0,1,2} << QList<int>{0,0,1};
        QTest::newRow("Japanese") << QByteArray("0") << 1u << QList<int>{0,1,2,100} << QList<int>{0,0,0,0};
        QTest::newRow("Russian") << QByteArray("n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2") << 3u << QList<int>{0,1,2,5,11,21,22,101} << QList<int>{2,0,1,2,2,0,1,0};
        QTest::newRow("Arabic") << QByteArray("n==0 ? 0 : n==1 ? 1 : n==2 ? 2 : n%100>=3 && n%100<=10 ? 3 : n%100>=11 ? 4 : 5") << 6u << QList<int>{0,1,2,3,10,11,99,100,102} << QList<int>{0,1,2,3,3,4,4,5,5};
        QTest::newRow("Slovenian") << QByteArray("n%100==1 ? 1 : n%100==2 ? 2 : n%100==3 || n%100==4 ? 3 : 0") << 4u << QList<int>{0,1,2,3,4,5,101} << QList<int>{0,1,2,3,3,0,1};
        QTest::newRow("short circuit") << QByteArray("n == 0 || 10 / n > 1 ? 0 : 1") << 2u << QList<int>{0,1,10,100} << QList<int>{0,0,1,1};
        QTest::newRow("precedence") << QByteArray("n != 1 && (n + 2 * 3 - 6) >= 2 ? 1 : 0") << 2u << QList<int>{0,1,2,100} << QList<int>{0,0,1,1};
        QTest::newRow("whitespace") << QByteArray("n\t!=\t1") << 2u << QList<int>{0,1,2} << QList<int>{1,0,1};
    }
    void contexts_data()
    {
        QTest::addColumn<bool>("bigEndian");
        QTest::newRow("little endian") << false;
        QTest::newRow("big endian") << true;
    }
    void contexts()
    {
        QFETCH(bool, bigEndian);
        GettextTranslator translator;
        QVERIFY(load(translator, catalog("n != 1", 2, bigEndian)));
        QCOMPARE(translator.translate("Settings", "Recording"), QStringLiteral("Enregistrement"));
        QCOMPARE(translator.translate("Player", "Recording"), QStringLiteral("Enregistrement en cours"));
        QCOMPARE(translator.translate(nullptr, "Recording"), QStringLiteral("Enregistrement en cours"));
        QVERIFY(translator.translateInContext("Player", "Recording").isNull());
        QVERIFY(translator.translateInContext(nullptr, "Recording").isNull());
        QCOMPARE(translator.translate("Settings", "Only here"), QStringLiteral("Uniquement ici"));
        QVERIFY(translator.translate("Player", "Only here").isNull());
        QCOMPARE(translator.translate("Settings", "%n item", "%n items", 1), QStringLiteral("settings0 %n"));
        QCOMPARE(translator.translate("Settings", "%n item", "%n items", 2), QStringLiteral("settings1 %n"));
        QCOMPARE(translator.translate("Player", "%n item", "%n items", 2), QStringLiteral("form1 %n"));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCOMPARE(QCoreApplication::translate("Settings", "Recording"), QStringLiteral("Enregistrement"));
        QCOMPARE(QCoreApplication::translate("Settings", "%n item", "%n items", 2), QStringLiteral("settings1 2"));
        QCoreApplication::removeTranslator(&translator);
    }
    void plurals()
    {
        QFETCH(QByteArray, rule); QFETCH(unsigned, forms);
        QFETCH(QList<int>, counts); QFETCH(QList<int>, expected);
        GettextTranslator translator;
        QVERIFY(load(translator, catalog(rule, forms)));
        for (qsizetype i = 0; i < counts.size(); ++i)
            QCOMPARE(translator.translate("", "%n item", "%n items", counts[i]), QStringLiteral("form%1 %n").arg(expected[i]));
    }
    void qtReplacesTheCountAfterTranslation()
    {
        GettextTranslator translator;
        QVERIFY(load(translator, catalog()));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto translated = QCoreApplication::translate("Queue", "%n item", "%n items", 5);
        QCoreApplication::removeTranslator(&translator);
        QCOMPARE(translated, QStringLiteral("form1 5"));
    }
    void failedLoadPreservesThePreviousCatalog()
    {
        GettextTranslator translator;
        QVERIFY(load(translator, catalog()));
        QVERIFY(!translator.loadCatalog(QStringLiteral("/no-such-vlc-catalog.mo")));
        QVERIFY(!load(translator, QByteArray("truncated")));
        QCOMPARE(translator.translate("", "Open file"), QStringLiteral("Ouvrir un fichier"));
    }
    void rejectsUnsafeCatalogs_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        auto bytes = catalog(); qToLittleEndian<quint32>(0xffffffff, bytes.data() + 8);
        QTest::newRow("oversized table") << bytes;
        bytes = catalog(); qToLittleEndian<quint32>(0xffffffff, bytes.data() + 32);
        QTest::newRow("offset outside file") << bytes;
        bytes = catalog(); bytes.chop(1);
        QTest::newRow("missing string terminator") << bytes;
        QTest::newRow("zero plural forms") << catalog("0", 0);
        QTest::newRow("too many plural forms") << catalog("0", 17);
        QTest::newRow("expression cannot execute code") << catalog("n; dangerous()");
        QTest::newRow("division by zero") << catalog("n / 0");
        QTest::newRow("out of range plural") << catalog("10");
        QTest::newRow("incomplete plural metadata") << catalog("n != 1", 2).replace("nplurals=2", "nplurals=invalid");
        QTest::newRow("deep nesting") << catalog(QByteArray(100, '(') + "n" + QByteArray(100, ')'));
    }
    void rejectsUnsafeCatalogs()
    {
        QFETCH(QByteArray, bytes);
        GettextTranslator translator;
        QVERIFY(!load(translator, bytes));
        QVERIFY(translator.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestGettextTranslator)
#include "test_gettext_translator.moc"
