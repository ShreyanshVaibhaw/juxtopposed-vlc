// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_INTERFACE_TRANSLATOR_HPP
#define VLC_QT_INTERFACE_TRANSLATOR_HPP

#include <QReadWriteLock>
#include <QStringList>
#include <QTranslator>
#include <memory>

class QQmlEngine;

class InterfaceTranslator final : public QTranslator
{
    Q_OBJECT
public:
    InterfaceTranslator(QString catalogDirectory, QStringList systemLanguages,
                        const QString& startupLanguage, QObject* parent = nullptr);
    ~InterfaceTranslator() override;
    static InterfaceTranslator* instance();

    // Prepare before saving preferences; activate only after the save succeeds.
    bool prepareLanguage(const QString& language);
    bool activatePreparedLanguage();
    QString requestedLanguage() const;
    QString activeLanguage() const;
    void watchEngine(QQmlEngine* engine);
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* source,
                      const char* disambiguation = nullptr, int n = -1) const override;

signals:
    void languageChanged();

private:
    struct Language;
    const QString m_directory;
    const QStringList m_systemLanguages;
    mutable QReadWriteLock m_lock;
    std::unique_ptr<Language> m_active;
    std::unique_ptr<Language> m_prepared;
};
#endif
