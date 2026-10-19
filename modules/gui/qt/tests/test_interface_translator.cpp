// SPDX-License-Identifier: GPL-2.0-or-later
#include "util/interface_translator.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QMap>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

class TestInterfaceTranslator : public QObject
{
    Q_OBJECT
    QTemporaryDir m_directory;
    QLocale m_locale;
    Qt::LayoutDirection m_direction = QGuiApplication::layoutDirection();

    bool writeCatalog(const QString& language, QMap<QByteArray, QByteArray> messages)
    {
        messages.insert({}, "Language: " + language.toUtf8() + "\nContent-Type: text/plain; charset=UTF-8\nPlural-Forms: nplurals=2; plural=n != 1;\n");
        const auto count = quint32(messages.size());
        QByteArray data(int(28 + count * 16), '\0');
        const auto word = [&](unsigned offset, quint32 value) { qToLittleEndian(value, data.data() + offset); };
        word(0, 0x950412de); word(8, count); word(12, 28); word(16, 28 + count * 8);
        unsigned i = 0;
        for (auto it = messages.cbegin(); it != messages.cend(); ++it, ++i) {
            word(28 + i * 8, quint32(it.key().size())); word(32 + i * 8, quint32(data.size()));
            data.append(it.key()); data.append('\0');
            word(28 + count * 8 + i * 8, quint32(it.value().size()));
            word(32 + count * 8 + i * 8, quint32(data.size()));
            data.append(it.value()); data.append('\0');
        }
        const auto path = m_directory.filePath(language + QStringLiteral("/LC_MESSAGES"));
        if (!QDir().mkpath(path)) return false;
        QFile file(path + QStringLiteral("/vlc.mo"));
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
    static QString text(const char* source) { return QCoreApplication::translate("Test", source); }
private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        QVERIFY(writeCatalog(QStringLiteral("fr"), {{"Open file", "Ouvrir un fichier"}, {"French only", "En français"},
            {"Recording", "Enregistrement en cours"}, {"Settings\004Recording", "Enregistrement"}}));
        QVERIFY(writeCatalog(QStringLiteral("fr_CA"), {{"Open file", "Ouvrir un fichier au Québec"},
            {"Recording", "Enregistrement en cours au Québec"}}));
        QVERIFY(writeCatalog(QStringLiteral("ar"), {{"Open file", QStringLiteral("افتح ملفًا").toUtf8()}}));
        QVERIFY(writeCatalog(QStringLiteral("sr@latin"), {{"Open file", "Otvori datoteku"}}));
    }
    void cleanup()
    {
        QLocale::setDefault(m_locale);
        QGuiApplication::setLayoutDirection(m_direction);
    }
    void switchesWithoutLeakingPreviousTranslations()
    {
        InterfaceTranslator translator(m_directory.path(), {QStringLiteral("fr")}, QStringLiteral("en"));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QSignalSpy changed(&translator, &InterfaceTranslator::languageChanged);
        QCOMPARE(text("Open file"), QStringLiteral("Open file"));
        QVERIFY(translator.prepareLanguage(QStringLiteral("fr")));
        QCOMPARE(text("Open file"), QStringLiteral("Open file"));
        QCOMPARE(changed.count(), 0);
        QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(text("Open file"), QStringLiteral("Ouvrir un fichier"));
        QCOMPARE(text("French only"), QStringLiteral("En français"));
        QVERIFY(translator.prepareLanguage(QStringLiteral("ar")));
        QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(text("Open file"), QStringLiteral("افتح ملفًا"));
        QCOMPARE(text("French only"), QStringLiteral("French only"));
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::RightToLeft);
        QVERIFY(translator.prepareLanguage(QStringLiteral("en")));
        QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(text("Open file"), QStringLiteral("Open file"));
        QCOMPARE(QGuiApplication::layoutDirection(), Qt::LeftToRight);
        QCOMPARE(QCoreApplication::translate("Test", "%n item", "%n items", 2), QStringLiteral("2 items"));
        QCOMPARE(QCoreApplication::translate("Test", "%n item", "%n items", 1), QStringLiteral("1 item"));
        QCOMPARE(QCoreApplication::translate("Test", "%n files", "", 2), QStringLiteral("2 files"));
    }
    void regionalAndOriginalSystemFallback_data()
    {
        QTest::addColumn<QStringList>("system"); QTest::addColumn<QString>("requested");
        QTest::addColumn<QString>("expected"); QTest::addColumn<QString>("effective");
        QTest::newRow("regional parent") << QStringList{} << QStringLiteral("fr-BE.UTF-8") << QStringLiteral("Ouvrir un fichier") << QStringLiteral("fr");
        QTest::newRow("regional catalog") << QStringList{} << QStringLiteral("fr_CA") << QStringLiteral("Ouvrir un fichier au Québec") << QStringLiteral("fr_CA");
        QTest::newRow("original system list") << QStringList{QStringLiteral("zz"), QStringLiteral("fr"), QStringLiteral("ar")} << QStringLiteral("auto") << QStringLiteral("Ouvrir un fichier") << QStringLiteral("fr");
        QTest::newRow("system C") << QStringList{QStringLiteral("C.UTF-8"), QStringLiteral("fr")} << QStringLiteral("auto") << QStringLiteral("Open file") << QStringLiteral("en");
        QTest::newRow("missing system catalog") << QStringList{QStringLiteral("zz")} << QStringLiteral("auto") << QStringLiteral("Open file") << QStringLiteral("en");
        QTest::newRow("modifier fallback") << QStringList{} << QStringLiteral("sr_RS.UTF-8@latin") << QStringLiteral("Otvori datoteku") << QStringLiteral("sr@latin");
    }
    void regionalAndOriginalSystemFallback()
    {
        QFETCH(QStringList, system); QFETCH(QString, requested); QFETCH(QString, expected); QFETCH(QString, effective);
        InterfaceTranslator translator(m_directory.path(), system, QStringLiteral("ar"));
        QVERIFY(translator.prepareLanguage(requested)); QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(translator.translate("Test", "Open file"), expected);
        QCOMPARE(translator.activeLanguage(), effective);
        QCOMPARE(translator.requestedLanguage(), requested);
        if (effective == QLatin1String("fr_CA"))
            QCOMPARE(translator.translate("Test", "French only"), QStringLiteral("En français"));
    }
    void failedPreparationKeepsActiveLanguage_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("missing") << QStringLiteral("zz");
        QTest::newRow("path traversal") << QStringLiteral("../fr");
        QTest::newRow("absolute path") << QStringLiteral("/fr");
        QTest::newRow("corrupt catalog") << QStringLiteral("de");
    }
    void switchesBackToSystemFromEitherStartup_data()
    {
        QTest::addColumn<QString>("system");
        QTest::addColumn<QString>("startup");
        for (const auto& system : {QStringLiteral("en"), QStringLiteral("fr"), QStringLiteral("ar")})
            for (const auto& startup : {QStringLiteral("auto"), QStringLiteral("fr"), QStringLiteral("ar")})
                QTest::newRow(qPrintable(system + QLatin1Char('/') + startup)) << system << startup;
    }
    void switchesBackToSystemFromEitherStartup()
    {
        QFETCH(QString, system); QFETCH(QString, startup);
        InterfaceTranslator translator(m_directory.path(), {system}, startup);
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto expected = [](const QString& language) {
            return language == QLatin1String("fr") ? QStringLiteral("Ouvrir un fichier")
                : language == QLatin1String("ar") ? QStringLiteral("افتح ملفًا") : QStringLiteral("Open file");
        };
        QCOMPARE(text("Open file"), expected(startup == QLatin1String("auto") ? system : startup));
        for (const auto& selected : {QStringLiteral("auto"), QStringLiteral("fr"), QStringLiteral("auto"),
                                     QStringLiteral("ar"), QStringLiteral("auto"), QStringLiteral("en"), QStringLiteral("auto")}) {
            QVERIFY(translator.prepareLanguage(selected));
            QVERIFY(translator.activatePreparedLanguage());
            const auto effective = selected == QLatin1String("auto") ? system : selected;
            QCOMPARE(text("Open file"), expected(effective));
            QCOMPARE(translator.activeLanguage(), effective);
            QCOMPARE(translator.requestedLanguage(), selected);
            QCOMPARE(QGuiApplication::layoutDirection(), QLocale(effective).textDirection());
        }
    }
    void failedPreparationKeepsActiveLanguage()
    {
        QFETCH(QString, language);
        QVERIFY(QDir().mkpath(m_directory.filePath(QStringLiteral("de/LC_MESSAGES"))));
        QFile file(m_directory.filePath(QStringLiteral("de/LC_MESSAGES/vlc.mo")));
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("invalid"), 7); file.close();
        InterfaceTranslator translator(m_directory.path(), {}, QStringLiteral("fr"));
        QVERIFY(translator.prepareLanguage(QStringLiteral("ar")));
        QVERIFY(!translator.prepareLanguage(language));
        QVERIFY(!translator.activatePreparedLanguage());
        QCOMPARE(translator.activeLanguage(), QStringLiteral("fr"));
        QCOMPARE(translator.translate("Test", "Open file"), QStringLiteral("Ouvrir un fichier"));
    }
    void retranslatesMultipleEnginesAndKeepsTheirState()
    {
        InterfaceTranslator translator(m_directory.path(), {}, QStringLiteral("en"));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QQmlEngine first, second;
        translator.watchEngine(&first); translator.watchEngine(&second); translator.watchEngine(&first);
        const QByteArray source = "import QtQml\nQtObject { property string label: qsTr(\"Open file\"); property int position: 22 }";
        QQmlComponent a(&first), b(&second);
        a.setData(source, QUrl()); b.setData(source, QUrl());
        std::unique_ptr<QObject> left(a.create()), right(b.create());
        QVERIFY2(left, qPrintable(a.errorString())); QVERIFY2(right, qPrintable(b.errorString()));
        QVERIFY(left->setProperty("position", 75));
        QVERIFY(translator.prepareLanguage(QStringLiteral("fr"))); QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(left->property("label").toString(), QStringLiteral("Ouvrir un fichier"));
        QCOMPARE(right->property("label").toString(), QStringLiteral("Ouvrir un fichier"));
        QCOMPARE(left->property("position").toInt(), 75);
        QCOMPARE(right->property("position").toInt(), 22);
        QVERIFY(translator.prepareLanguage(QStringLiteral("ar"))); QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(left->property("label").toString(), QStringLiteral("افتح ملفًا"));
        QCOMPARE(left->property("position").toInt(), 75);
    }
    void destroysInstalledTranslatorSafely()
    {
        {
            InterfaceTranslator translator(m_directory.path(), {}, QStringLiteral("fr"));
            QVERIFY(QCoreApplication::installTranslator(&translator));
            QCOMPARE(text("Open file"), QStringLiteral("Ouvrir un fichier"));
        }
        QCOMPARE(text("Open file"), QStringLiteral("Open file"));
    }
    void contextualQmlRetranslatesWithoutChangingState()
    {
        InterfaceTranslator translator(m_directory.path(), {}, QStringLiteral("en"));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QQmlEngine engine;
        translator.watchEngine(&engine);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject { property string status: qsTr(\"Recording\"); property string section: qsTranslate(\"Settings\", \"Recording\"); property int position: 22 }", QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        QCOMPARE(object->property("status").toString(), QStringLiteral("Recording"));
        QCOMPARE(object->property("section").toString(), QStringLiteral("Recording"));
        QVERIFY(translator.prepareLanguage(QStringLiteral("fr_CA")));
        QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(object->property("status").toString(), QStringLiteral("Enregistrement en cours au Québec"));
        QCOMPARE(object->property("section").toString(), QStringLiteral("Enregistrement"));
        QVERIFY(translator.prepareLanguage(QStringLiteral("fr")));
        QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(object->property("status").toString(), QStringLiteral("Enregistrement en cours"));
        QCOMPARE(object->property("section").toString(), QStringLiteral("Enregistrement"));
        QVERIFY(translator.prepareLanguage(QStringLiteral("en")));
        QVERIFY(translator.activatePreparedLanguage());
        QCOMPARE(object->property("status").toString(), QStringLiteral("Recording"));
        QCOMPARE(object->property("section").toString(), QStringLiteral("Recording"));
        QCOMPARE(object->property("position").toInt(), 22);
    }
};
QTEST_MAIN(TestInterfaceTranslator)
#include "test_interface_translator.moc"
