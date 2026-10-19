// SPDX-License-Identifier: GPL-2.0-or-later
#include "interface_translator.hpp"
#include "gettext_translator.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QRegularExpression>
#include <vector>

struct InterfaceTranslator::Language
{
    QString requested;
    QString effective = QStringLiteral("en");
    std::vector<std::unique_ptr<GettextTranslator>> catalogs;
};

namespace {
QStringList localeCandidates(QString code)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z][A-Za-z0-9_.@-]{0,63}$"));
    if (!safe.match(code).hasMatch() || code.contains(QLatin1String(".."))) return {};
    const auto modifier = code.section(QLatin1Char('@'), 1);
    code = code.section(QLatin1Char('@'), 0, 0).section(QLatin1Char('.'), 0, 0);
    code.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (code == QLatin1String("C") || code == QLatin1String("POSIX")) return {QStringLiteral("en")};
    const auto base = code.section(QLatin1Char('_'), 0, 0);
    QStringList candidates;
    if (!modifier.isEmpty()) candidates << code + QLatin1Char('@') + modifier << base + QLatin1Char('@') + modifier;
    candidates << code << base;
    candidates.removeDuplicates();
    return candidates;
}
}

InterfaceTranslator::InterfaceTranslator(QString directory, QStringList systemLanguages,
                                         const QString& startupLanguage, QObject* parent)
    : QTranslator(parent), m_directory(std::move(directory)), m_systemLanguages(std::move(systemLanguages))
{
    setObjectName(QStringLiteral("vlcInterfaceTranslator"));
    if (!prepareLanguage(startupLanguage)) prepareLanguage(QStringLiteral("en"));
    m_active = std::move(m_prepared);
}

InterfaceTranslator::~InterfaceTranslator()
{
    if (qApp) QCoreApplication::removeTranslator(this);
}

InterfaceTranslator* InterfaceTranslator::instance()
{
    return qApp ? qApp->findChild<InterfaceTranslator*>(QStringLiteral("vlcInterfaceTranslator"), Qt::FindDirectChildrenOnly) : nullptr;
}

bool InterfaceTranslator::prepareLanguage(const QString& language)
{
    auto prepared = std::make_unique<Language>();
    prepared->requested = language;
    const auto requested = language == QLatin1String("auto") ? m_systemLanguages : QStringList{language};
    QStringList attempted;
    bool found = false;
    for (const auto& code : requested) {
        for (const auto& candidate : localeCandidates(code)) {
            if (attempted.contains(candidate)) continue;
            attempted.append(candidate);
            if (candidate == QLatin1String("en")) {
                if (!found) prepared->effective = candidate;
                found = true;
                break;
            }
            const auto path = QDir(m_directory).filePath(candidate + QStringLiteral("/LC_MESSAGES/vlc.mo"));
            if (QFileInfo::exists(path)) {
                auto catalog = std::make_unique<GettextTranslator>();
                if (!catalog->loadCatalog(path)) { m_prepared.reset(); return false; }
                if (!found) prepared->effective = candidate;
                prepared->catalogs.push_back(std::move(catalog));
                found = true;
            }
        }
        // Other system preferences are fallback languages, not sources for
        // mixing missing strings into an already selected UI language.
        if (found) break;
    }
    if (!found && language != QLatin1String("auto")) { m_prepared.reset(); return false; }
    m_prepared = std::move(prepared);
    return true;
}

bool InterfaceTranslator::activatePreparedLanguage()
{
    if (!m_prepared) return false;
    std::unique_ptr<Language> previous;
    QString effective;
    {
        QWriteLocker lock(&m_lock);
        previous = std::move(m_active);
        m_active = std::move(m_prepared);
        effective = m_active->effective;
    }
    // QTranslator destruction can acquire Qt's application translator lock.
    // Do not hold our state lock while an old catalog is destroyed.
    previous.reset();
    const QLocale locale(effective);
    QLocale::setDefault(locale);
    QEvent change(QEvent::LanguageChange);
    QCoreApplication::sendEvent(qApp, &change);
    QGuiApplication::setLayoutDirection(locale.textDirection());
    emit languageChanged();
    return true;
}

QString InterfaceTranslator::requestedLanguage() const
{
    QReadLocker lock(&m_lock);
    return m_active->requested;
}

QString InterfaceTranslator::activeLanguage() const
{
    QReadLocker lock(&m_lock);
    return m_active->effective;
}

void InterfaceTranslator::watchEngine(QQmlEngine* engine)
{
    connect(this, &InterfaceTranslator::languageChanged, engine, &QQmlEngine::retranslate, Qt::UniqueConnection);
}

QString InterfaceTranslator::translate(const char* context, const char* source, const char* disambiguation, int n) const
{
    if (!source) return {};
    QReadLocker lock(&m_lock);
    if (qstrcmp(source, "QT_LAYOUT_DIRECTION") == 0)
        return QLocale(m_active->effective).textDirection() == Qt::RightToLeft ? QStringLiteral("RTL") : QStringLiteral("LTR");
    // A parent catalog's explicit context wins over a regional shared message.
    if (context && *context) {
        for (const auto& catalog : m_active->catalogs) {
            auto translated = catalog->translateInContext(context, source, n);
            if (!translated.isNull()) return translated;
        }
    }
    for (const auto& catalog : m_active->catalogs) {
        auto translated = catalog->translate(nullptr, source, disambiguation, n);
        if (!translated.isNull()) return translated;
    }
    return QString::fromUtf8(n >= 0 && n != 1 && disambiguation && *disambiguation ? disambiguation : source);
}
