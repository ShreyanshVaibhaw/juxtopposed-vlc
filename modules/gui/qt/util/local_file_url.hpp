// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QUrl>

namespace vlc::qt {
inline QUrl normalizeLocalFileUrl(const QUrl& url)
{
    // VLC and file dialogs can escape reserved filename characters differently.
    // Compare their local paths without resolving links or accessing the disk.
    if (url.isValid() && url.isLocalFile() && !url.hasQuery() && !url.hasFragment())
        return QUrl::fromLocalFile(url.toLocalFile());
    return url;
}
}
