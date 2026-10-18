// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_POSTPROCESSING_PREFERENCES_HPP
#define VLC_QT_POSTPROCESSING_PREFERENCES_HPP

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <QByteArray>
#include <QStringList>
#include <algorithm>
#include <cstdlib>
#include <memory>

namespace vlc::postprocessing {

inline bool isFilter(const char *name)
{
    return name && (!strcmp(name, "postproc") || !strcmp(name, "postprocess") || !strcmp(name, "pp"));
}

// Use VLC's parser: colons inside quoted or nested options are not separators.
// Keep unrelated entries verbatim, including options which this editor does not know.
template<class Visitor>
void visit(const QString &chain, Visitor visitor)
{
    auto remaining = chain.toUtf8();
    while (!remaining.isEmpty()) {
        char *name = nullptr;
        config_chain_t *options = nullptr;
        std::unique_ptr<char, decltype(&free)> next(
            config_ChainCreate(&name, &options, remaining.constData()), free);
        std::unique_ptr<char, decltype(&free)> ownedName(name, free);
        std::unique_ptr<config_chain_t, decltype(&config_ChainDestroy)> ownedOptions(options, config_ChainDestroy);
        const auto length = remaining.size() - (next ? strlen(next.get()) + 1 : 0);
        visitor(remaining.left(length), name, options);
        remaining = next ? QByteArray(next.get()) : QByteArray();
    }
}

// -1 represents a chain with different levels. Zero also covers an absent filter.
inline int quality(const QString &chain, int fallback, int maximum)
{
    int result = 0;
    bool found = false;
    visit(chain, [&](const QByteArray &, const char *name, const config_chain_t *options) {
        if (!isFilter(name)) return;
        int level = std::clamp(fallback, 0, maximum);
        for (auto *option = options; option; option = option->p_next)
            if (option->psz_name && !strcmp(option->psz_name, "q") && option->psz_value)
                // Match config_ChainParse's integer syntax, range and last-value policy.
                level = std::clamp<long long>(strtoll(option->psz_value, nullptr, 0), 0, maximum);
        result = found && result != level ? -1 : level;
        found = true;
    });
    return result;
}

inline QString withQuality(const QString &chain, int level)
{
    QStringList entries;
    bool found = false;
    visit(chain, [&](const QByteArray &raw, const char *name, const config_chain_t *options) {
        if (!isFilter(name)) {
            entries.append(QString::fromUtf8(raw));
            return;
        }
        found = true;
        if (level == 0) return;
        QStringList parameters;
        for (auto *option = options; option; option = option->p_next) {
            if (!option->psz_name || !strcmp(option->psz_name, "q")) continue;
            auto parameter = QString::fromUtf8(option->psz_name);
            if (option->psz_value) {
                std::unique_ptr<char, decltype(&free)> escaped(config_StringEscape(option->psz_value), free);
                parameter += QStringLiteral("=\"") + QString::fromUtf8(escaped.get()) + QLatin1Char('"');
            }
            parameters.append(parameter);
        }
        parameters.append(QStringLiteral("q=%1").arg(level));
        entries.append(QString::fromUtf8(name) + QLatin1Char('{') + parameters.join(QLatin1Char(',')) + QLatin1Char('}'));
    });
    if (!found && level != 0) entries.prepend(QStringLiteral("postproc{q=%1}").arg(level));
    return entries.join(QLatin1Char(':'));
}

}
#endif
