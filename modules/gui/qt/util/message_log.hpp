// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_MESSAGE_LOG_HPP
#define VLC_QT_MESSAGE_LOG_HPP

#include <vlc_common.h>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>

struct MessageLogEntry
{
    int priority;
    QString module;
    QString text;
};

inline bool messageLogMatches(const QString& text, const QString& filter)
{
    return filter.isEmpty() || text.contains(filter, Qt::CaseInsensitive);
}

// Append through a document cursor, not the user's selection. Point cursors
// keep selection boundaries stable even when the selection ends at the old EOF.
inline void appendMessageLog(QPlainTextEdit* view, const QList<MessageLogEntry>& entries,
                             const QString& filter, const QColor& accent)
{
    if (entries.isEmpty()) return;
    auto* document = view->document();
    auto* scroll = view->verticalScrollBar();
    const int previousScroll = scroll->value();
    const auto selection = view->textCursor();
    const bool follow = previousScroll == scroll->maximum() && !selection.hasSelection();
    QTextCursor position(document), anchor(document);
    position.setPosition(selection.position());
    anchor.setPosition(selection.anchor());
    position.setKeepPositionOnInsert(true);
    anchor.setKeepPositionOnInsert(true);

    const bool dark = view->palette().color(QPalette::Base).lightness() < 128;
    const QColor sourceColor = dark ? accent : accent.darker(200);
    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    for (const auto& entry : entries)
    {
        if (!document->isEmpty()) cursor.insertBlock();
        const auto firstBlock = cursor.block();
        QTextCharFormat format;
        format.setFontItalic(true);
        format.setForeground(sourceColor);
        cursor.insertText(entry.module, format);
        QString level;
        QColor levelColor;
        switch (entry.priority)
        {
        case VLC_MSG_INFO:
            level = QStringLiteral(" info: "); levelColor = sourceColor; break;
        case VLC_MSG_ERR:
            level = QStringLiteral(" error: "); levelColor = QColor(dark ? "#ff7373" : "#a31b1b"); break;
        case VLC_MSG_WARN:
            level = QStringLiteral(" warning: "); levelColor = QColor(dark ? "#d69c38" : "#775100"); break;
        default:
            level = QStringLiteral(" debug: "); levelColor = view->palette().color(QPalette::WindowText); break;
        }
        format.setForeground(levelColor);
        cursor.insertText(level, format);
        format.setFontItalic(false);
        format.setForeground(view->palette().color(QPalette::Text));
        cursor.insertText(entry.text, format);
        for (auto block = firstBlock; block.isValid(); block = block.next())
            block.setVisible(messageLogMatches(block.text(), filter));
    }
    cursor.endEditBlock();

    if (view->textCursor().position() != position.position()
        || view->textCursor().anchor() != anchor.position())
    {
        QTextCursor restored(document);
        restored.setPosition(anchor.position());
        restored.setPosition(position.position(), QTextCursor::KeepAnchor);
        view->setTextCursor(restored);
    }
    scroll->setValue(follow ? scroll->maximum() : previousScroll);
}

#endif
