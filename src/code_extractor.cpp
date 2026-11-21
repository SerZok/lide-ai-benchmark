#include "code_extractor.h"

#include <QRegularExpression>

namespace
{
bool isDefinitionAt(const QString &text, qsizetype position)
{
    static const QRegularExpression definition(
        QStringLiteral("^\\(\\s*(?:defun|defmacro)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return definition.match(text.mid(position)).hasMatch();
}

qsizetype formEnd(const QString &text, qsizetype start)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    bool lineComment = false;
    int blockCommentDepth = 0;

    for (qsizetype i = start; i < text.size(); ++i)
    {
        const QChar current = text.at(i);
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar();

        if (lineComment)
        {
            if (current == QLatin1Char('\n'))
                lineComment = false;
            continue;
        }
        if (blockCommentDepth > 0)
        {
            if (current == QLatin1Char('#') && next == QLatin1Char('|'))
            {
                ++blockCommentDepth;
                ++i;
            }
            else if (current == QLatin1Char('|') && next == QLatin1Char('#'))
            {
                --blockCommentDepth;
                ++i;
            }
            continue;
        }
        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (current == QLatin1Char('\\'))
                escaped = true;
            else if (current == QLatin1Char('"'))
                inString = false;
            continue;
        }
        if (current == QLatin1Char(';'))
            lineComment = true;
        else if (current == QLatin1Char('#') && next == QLatin1Char('|'))
        {
            blockCommentDepth = 1;
            ++i;
        }
        else if (current == QLatin1Char('#') && next == QLatin1Char('\\'))
        {
            qsizetype characterPosition = i + 2;
            if (characterPosition < text.size())
            {
                if (text.at(characterPosition).isLetterOrNumber())
                {
                    while (characterPosition < text.size() &&
                           !text.at(characterPosition).isSpace() &&
                           text.at(characterPosition) != QLatin1Char('(') &&
                           text.at(characterPosition) != QLatin1Char(')'))
                        ++characterPosition;
                }
                else
                    ++characterPosition;
                i = characterPosition - 1;
            }
        }
        else if (current == QLatin1Char('"'))
            inString = true;
        else if (current == QLatin1Char('('))
            ++depth;
        else if (current == QLatin1Char(')') && --depth == 0)
            return i + 1;
    }
    return -1;
}
}

QString CodeExtractor::extract(const QString &response)
{
    static const QRegularExpression fencedBlock(
        QStringLiteral("^[ \\t]*```[ \\t]*(?:lisp|common-lisp|cl)[ \\t]*\\r?\\n"
                       "([\\s\\S]*?)\\r?\\n[ \\t]*```[ \\t]*$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const auto fencedMatch = fencedBlock.match(response);
    if (fencedMatch.hasMatch())
        return fencedMatch.captured(1).trimmed();

    qsizetype start = -1;
    for (qsizetype i = 0; i < response.size(); ++i)
    {
        if (response.at(i) == QLatin1Char('(') && isDefinitionAt(response, i))
        {
            start = i;
            break;
        }
    }
    if (start < 0)
        return {};

    const qsizetype end = formEnd(response, start);
    if (end < 0)
        return response.mid(start).trimmed();

    QString result = response.mid(start, end - start);
    qsizetype cursor = end;
    while (cursor < response.size())
    {
        while (cursor < response.size() &&
               (response.at(cursor).isSpace() || response.at(cursor) == QLatin1Char(';')))
        {
            if (response.at(cursor) == QLatin1Char(';'))
            {
                while (cursor < response.size() && response.at(cursor) != QLatin1Char('\n'))
                    ++cursor;
            }
            else
                ++cursor;
        }
        if (cursor >= response.size() || !isDefinitionAt(response, cursor))
            break;
        const qsizetype nextEnd = formEnd(response, cursor);
        if (nextEnd < 0)
            break;
        result += QLatin1Char('\n') + response.mid(cursor, nextEnd - cursor);
        cursor = nextEnd;
    }
    return result.trimmed();
}
