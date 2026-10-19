// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_MESSAGE_LOG_HPP
#define VLC_QT_MESSAGE_LOG_HPP

#include "message_log_buffer.hpp"
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>

inline constexpr int MessageLogHistoryLines = 5000;
inline constexpr int MessageLogHistoryCharacters = 2 * 1024 * 1024;

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
    // Undo commands retain removed text, even in a read-only widget.
    view->setUndoRedoEnabled(false);
    auto* document = view->document();
    auto* scroll = view->verticalScrollBar();
    const int previousScroll = scroll->value();
    const auto selection = view->textCursor();
    const bool follow = previousScroll == scroll->maximum() && !selection.hasSelection();
    auto viewport = view->cursorForPosition(QPoint(0, 0));
    viewport.setKeepPositionOnInsert(true);
    const int viewportPosition = viewport.position();
    const int viewportTop = view->cursorRect(viewport).top();
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
    auto firstAdded = cursor;
    firstAdded.setKeepPositionOnInsert(true);
    for (const auto& entry : entries)
    {
        if (!document->isEmpty()) cursor.insertBlock();
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
    }
    int remove = document->blockCount() > MessageLogHistoryLines
        ? document->findBlockByNumber(document->blockCount() - MessageLogHistoryLines).position() : 0;
    const int excess = document->characterCount() - 1 - MessageLogHistoryCharacters;
    if (excess > remove)
    {
        const auto block = document->findBlock(excess);
        remove = block.position() == excess ? excess
            : block.next().isValid() ? block.next().position() : excess;
        if (document->characterAt(remove).isLowSurrogate()
            && remove > 0 && document->characterAt(remove - 1).isHighSurrogate()) ++remove;
    }
    if (remove > 0)
    {
        QTextCursor trim(document);
        trim.setPosition(remove);
        trim.setPosition(0, QTextCursor::KeepAnchor);
        trim.removeSelectedText();
    }
    for (auto block = firstAdded.block(); block.isValid(); block = block.next())
        block.setVisible(messageLogMatches(block.text(), filter));
    cursor.endEditBlock();

    if (remove > 0 && !follow && viewportPosition >= remove)
    {
        // Resolve lazy line counts before using scrollbar coordinates. A
        // search across unlaid wrapped blocks can change its own coordinates.
        if (auto* layout = qobject_cast<QPlainTextDocumentLayout*>(document->documentLayout()))
            for (auto block = document->firstBlock(); block.isValid(); block = block.next())
                if (block.isVisible()) layout->ensureBlockLayout(block);
    }

    if (view->textCursor().position() != position.position()
        || view->textCursor().anchor() != anchor.position())
    {
        QTextCursor restored(document);
        restored.setPosition(anchor.position());
        restored.setPosition(position.position(), QTextCursor::KeepAnchor);
        view->setTextCursor(restored);
    }
    if (follow) scroll->setValue(scroll->maximum());
    else if (remove == 0) scroll->setValue(previousScroll);
    else if (viewportPosition < remove) scroll->setValue(scroll->minimum());
    else if (view->cursorRect(viewport).top() != viewportTop)
    {
        // Qt usually retains the visible block when earlier blocks are removed.
        // Restoring a selection can scroll it into view. Recover the old anchor
        // through the public scrollbar API, including wrapped and hidden lines.
        int low = scroll->minimum(), high = scroll->maximum();
        int best = scroll->value(), distance = qAbs(view->cursorRect(viewport).top() - viewportTop);
        while (low <= high)
        {
            const int middle = low + (high - low) / 2;
            scroll->setValue(middle);
            const int top = view->cursorRect(viewport).top();
            if (qAbs(top - viewportTop) < distance)
            {
                distance = qAbs(top - viewportTop);
                best = middle;
            }
            if (top == viewportTop) break;
            if (top > viewportTop) low = middle + 1;
            else high = middle - 1;
        }
        scroll->setValue(best);
    }
}

#endif
