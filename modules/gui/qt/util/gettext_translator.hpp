// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_GETTEXT_TRANSLATOR_HPP
#define VLC_QT_GETTEXT_TRANSLATOR_HPP

#include <QHash>
#include <QReadWriteLock>
#include <QStringList>
#include <QTranslator>

// Read the same MO catalogs as LibVLC without changing its process locale.
// A UI translator can then change language while decoder threads keep running.
class GettextTranslator final : public QTranslator
{
public:
    explicit GettextTranslator(QObject* parent = nullptr) : QTranslator(parent) {}
    bool loadCatalog(const QString& filename);
    bool isEmpty() const override;
    QString translate(const char* context, const char* source,
                      const char* disambiguation = nullptr, int n = -1) const override;
    // Query only this context; callers control shared-message fallback.
    QString translateInContext(const char* context, const char* source, int n = -1) const;
    QString catalogLanguage() const;

private:
    QString translateMessage(const QByteArray& key, int n) const;
    mutable QReadWriteLock m_lock;
    QHash<QByteArray, QStringList> m_messages;
    QByteArray m_plural = "n != 1";
    unsigned m_formCount = 2;
    QString m_language;
};
#endif
