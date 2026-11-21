#include "code_extractor.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QString fenced =
        "Intro text\n```common-lisp\n(defun answer (x)\n  (+ x 1))\n```\nAfterword";
    const QString extractedFenced = CodeExtractor::extract(fenced);
    if (extractedFenced != QStringLiteral("(defun answer (x)\n  (+ x 1))"))
    {
        qCritical() << "Fenced Lisp extraction failed:" << extractedFenced;
        return 1;
    }

    const QString plain =
        "Here is the implementation:\n"
        "(defun answer (x)\n"
        "  (format nil \"a ) string\") ; ignore )\n"
        "  (+ x 1))\n"
        "(defmacro twice (form) `(progn ,form ,form))\n"
        "This explanation is not code.";
    const QString extractedPlain = CodeExtractor::extract(plain);
    if (!extractedPlain.startsWith(QStringLiteral("(defun answer")) ||
        !extractedPlain.contains(QStringLiteral("(defmacro twice")) ||
        extractedPlain.contains(QStringLiteral("This explanation")))
    {
        qCritical() << "Plain Lisp extraction failed:" << extractedPlain;
        return 1;
    }

    if (!CodeExtractor::extract(QStringLiteral("No Lisp implementation here.")).isEmpty())
    {
        qCritical() << "Unexpected code extracted from prose.";
        return 1;
    }
    const QString malformed =
        CodeExtractor::extract(QStringLiteral("Attempt:\n(defun incomplete (x)\n  (+ x 1)"));
    if (!malformed.startsWith(QStringLiteral("(defun incomplete")) ||
        !malformed.contains(QStringLiteral("(+ x 1)")))
    {
        qCritical() << "Malformed Lisp was not preserved for sandbox classification.";
        return 1;
    }
    return 0;
}
