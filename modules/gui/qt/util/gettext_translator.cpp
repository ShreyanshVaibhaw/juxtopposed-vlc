// SPDX-License-Identifier: GPL-2.0-or-later
#include "gettext_translator.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QtEndian>
#include <limits>
#include <cstring>

namespace {
// GNU plural expressions use unsigned integer arithmetic and C precedence.
// Interpret only their expression grammar; catalogs must never execute code.
class PluralExpression
{
public:
    PluralExpression(const QByteArray& expression, quint64 count)
        : m_text(expression), m_count(count) {}
    bool evaluate(quint64& result)
    {
        if (m_text.isEmpty() || m_text.size() > 1024) return false;
        result = expression(0, true);
        whitespace();
        return m_valid && m_position == m_text.size();
    }
private:
    void whitespace()
    {
        while (m_position < m_text.size()) {
            const char c = m_text[m_position];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++m_position;
        }
    }
    bool consume(const char* text)
    {
        whitespace();
        const auto size = qsizetype(strlen(text));
        if (m_text.mid(m_position, size) != text) return false;
        m_position += size;
        return true;
    }
    quint64 unary(bool active)
    {
        if (++m_depth > 64) { m_valid = false; --m_depth; return 0; }
        quint64 value = 0;
        if (consume("!")) value = !unary(active);
        else if (consume("-")) value = quint64(0) - unary(active);
        else if (consume("+")) value = unary(active);
        else if (consume("(")) {
            value = expression(0, active);
            if (!consume(")")) m_valid = false;
        } else if (consume("n")) value = m_count;
        else {
            whitespace();
            const auto start = m_position;
            while (m_position < m_text.size() && m_text[m_position] >= '0' && m_text[m_position] <= '9') {
                const unsigned digit = unsigned(m_text[m_position++] - '0');
                if (value > (std::numeric_limits<quint64>::max() - digit) / 10) m_valid = false;
                value = value * 10 + digit;
            }
            if (m_position == start) m_valid = false;
        }
        --m_depth;
        return value;
    }
    quint64 expression(int minimum, bool active)
    {
        quint64 left = unary(active);
        while (m_valid) {
            struct Operator { const char* text; int precedence; };
            static constexpr Operator operators[] = {
                {"||", 1}, {"&&", 2}, {"==", 3}, {"!=", 3},
                {"<=", 4}, {">=", 4}, {"<", 4}, {">", 4},
                {"+", 5}, {"-", 5}, {"*", 6}, {"/", 6}, {"%", 6}
            };
            const Operator* selected = nullptr;
            for (const auto& op : operators) {
                if (op.precedence >= minimum && consume(op.text)) { selected = &op; break; }
            }
            if (!selected) break;
            const QByteArray op(selected->text);
            const bool rightActive = active && !(op == "||" && left) && !(op == "&&" && !left);
            const quint64 right = expression(selected->precedence + 1, rightActive);
            if (!active) { left = 0; continue; }
            if (op == "||") left = left || right;
            else if (op == "&&") left = left && right;
            else if (op == "==") left = left == right;
            else if (op == "!=") left = left != right;
            else if (op == "<=") left = left <= right;
            else if (op == ">=") left = left >= right;
            else if (op == "<") left = left < right;
            else if (op == ">") left = left > right;
            else if (op == "+") left += right;
            else if (op == "-") left -= right;
            else if (op == "*") left *= right;
            else if (right == 0) { m_valid = false; left = 0; }
            else if (op == "/") left /= right;
            else left %= right;
        }
        if (minimum == 0 && consume("?")) {
            if (++m_depth > 64) { m_valid = false; --m_depth; return 0; }
            const auto yes = expression(0, active && left != 0);
            if (!consume(":")) m_valid = false;
            const auto no = expression(0, active && left == 0);
            left = left ? yes : no;
            --m_depth;
        }
        return left;
    }
    const QByteArray& m_text;
    const quint64 m_count;
    qsizetype m_position = 0;
    int m_depth = 0;
    bool m_valid = true;
};
}

bool GettextTranslator::loadCatalog(const QString& filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 28 || file.size() > 32 * 1024 * 1024) return false;
    const auto bytes = file.readAll();
    const auto magic = qFromLittleEndian<quint32>(bytes.constData());
    if (magic != 0x950412de && magic != 0xde120495) return false;
    const auto word = [&](quint64 offset) {
        return magic == 0x950412de ? qFromLittleEndian<quint32>(bytes.constData() + offset)
                                 : qFromBigEndian<quint32>(bytes.constData() + offset);
    };
    if ((word(4) >> 16) > 1) return false;
    const quint64 count = word(8), originals = word(12), translations = word(16);
    if (count > 100000 || originals + count * 8 > quint64(bytes.size())
        || translations + count * 8 > quint64(bytes.size())) return false;
    const auto string = [&](quint64 table, quint64 index, QByteArray& result) {
        const quint64 size = word(table + index * 8), offset = word(table + index * 8 + 4);
        if (offset + size >= quint64(bytes.size()) || bytes[qsizetype(offset + size)] != '\0') return false;
        result = bytes.mid(offset, size);
        return true;
    };
    QHash<QByteArray, QStringList> messages;
    QByteArray header;
    for (quint64 i = 0; i < count; ++i) {
        QByteArray original, translated;
        if (!string(originals, i, original) || !string(translations, i, translated)) return false;
        if (original.isEmpty()) { header = translated; continue; }
        QStringList forms;
        for (const auto& text : translated.split('\0')) forms.append(QString::fromUtf8(text));
        messages.insert(original.split('\0').first(), std::move(forms));
    }
    const QString metadata = QString::fromUtf8(header);
    const auto plural = QRegularExpression(QStringLiteral("(?:^|\\n)Plural-Forms:[ \\t]*nplurals[ \\t]*=[ \\t]*(\\d+)[ \\t]*;[ \\t]*plural[ \\t]*=[ \\t]*([^\\r\\n]+);"), QRegularExpression::CaseInsensitiveOption).match(metadata);
    QByteArray expression = "n != 1";
    unsigned formCount = 2;
    if (metadata.contains(QStringLiteral("Plural-Forms:"), Qt::CaseInsensitive) && !plural.hasMatch()) return false;
    if (plural.hasMatch()) {
        formCount = plural.captured(1).toUInt();
        expression = plural.captured(2).trimmed().toLatin1();
        if (formCount < 1 || formCount > 16) return false;
    }
    for (const quint64 n : {0ull, 1ull, 2ull, 5ull, 11ull, 21ull, 101ull, 1000000ull}) {
        quint64 result;
        if (!PluralExpression(expression, n).evaluate(result) || result >= formCount) return false;
    }
    const auto language = QRegularExpression(QStringLiteral("(?:^|\\n)Language:[ \\t]*([^\\r\\n]+)"), QRegularExpression::CaseInsensitiveOption).match(metadata);
    QWriteLocker lock(&m_lock);
    m_messages = std::move(messages);
    m_plural = std::move(expression);
    m_formCount = formCount;
    m_language = language.captured(1).trimmed();
    return true;
}

bool GettextTranslator::isEmpty() const
{
    QReadLocker lock(&m_lock);
    return m_messages.isEmpty();
}

QString GettextTranslator::catalogLanguage() const
{
    QReadLocker lock(&m_lock);
    return m_language;
}

QString GettextTranslator::translate(const char* context, const char* source, const char*, int n) const
{
    const auto contextual = translateInContext(context, source, n);
    if (!contextual.isNull()) return contextual;
    if (!source || !*source) return {};
    // Existing GNU catalogs share uncontextualized messages between Qt classes.
    return translateMessage(QByteArray(source), n);
}

QString GettextTranslator::translateInContext(const char* context, const char* source, int n) const
{
    if (!context || !*context || !source || !*source) return {};
    QByteArray key(context);
    key.append('\x04');
    key.append(source);
    return translateMessage(key, n);
}

QString GettextTranslator::translateMessage(const QByteArray& key, int n) const
{
    QReadLocker lock(&m_lock);
    const auto found = m_messages.constFind(key);
    if (found == m_messages.cend()) return {};
    quint64 form = 0;
    if (n >= 0 && (!PluralExpression(m_plural, unsigned(n)).evaluate(form) || form >= m_formCount)) return {};
    if (form >= quint64(found->size()) || (*found)[qsizetype(form)].isEmpty()) return {};
    return (*found)[qsizetype(form)];
}
