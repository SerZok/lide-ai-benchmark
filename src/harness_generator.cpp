#include "harness_generator.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace
{
    QString escapeLispString(QString value)
    {
        value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
        value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        return value;
    }
}

QString HarnessGenerator::generate(const QJsonObject &task, const QString &sourcePath,
                                   QString *error)
{
    static const QRegularExpression functionNamePattern(
        QStringLiteral("^[A-Za-z0-9*+!$%&<=>?@^_~./:-]+$"));
    const QString functionName = task.value("function").toString();
    const QJsonArray testDefinitions = task.value("tests").toArray();
    if (!functionNamePattern.match(functionName).hasMatch() || testDefinitions.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Task requires a valid function and non-empty tests.");
        return {};
    }

    QVector<SbclSandbox::TestCase> tests;
    for (qsizetype index = 0; index < testDefinitions.size(); ++index)
    {
        const QJsonObject test = testDefinitions.at(index).toObject();
        if (!test.value("arguments").isString() || test.value("arguments").toString().isEmpty() ||
            !test.value("expected").isString() || test.value("expected").toString().isEmpty())
        {
            if (error)
                *error = QStringLiteral("Test %1 requires Lisp expressions for arguments and expected.")
                             .arg(index);
            return {};
        }
        tests.append({test.value("name").toString(QStringLiteral("test-%1").arg(index + 1)),
                      test.value("arguments").toString(),
                      test.value("expected").toString()});
    }
    if (error)
        error->clear();
    return generate(sourcePath, functionName, tests);
}

QString HarnessGenerator::generate(const QString &sourcePath, const QString &functionName,
                                   const QVector<SbclSandbox::TestCase> &tests)
{
    QString harness = QStringLiteral(
                          "(cl:setf cl:*read-eval* cl:nil)\n"
                          "(cl:let ((passed 0) (total %1))\n"
                          "  (cl:labels ((check (name expected thunk)\n"
                          "               (cl:handler-case\n"
                          "                   (cl:let ((actual (cl:funcall thunk)))\n"
                          "                     (cl:if (cl:equal expected actual)\n"
                          "                            (cl:progn (cl:incf passed) "
                          "(cl:format cl:*standard-output* \"TEST_PASS:~A~%\" name))\n"
                          "                            (cl:format cl:*standard-output* "
                          "\"TEST_FAIL:~A expected=~S actual=~S~%\" name expected actual)))\n"
                          "                 (cl:error (condition)\n"
                          "                   (cl:format cl:*standard-output* \"TEST_ERROR:~A ~A~%\" "
                          "name condition)))))\n"
                          "    (cl:handler-case\n"
                          "        (cl:let ((cl:*package* (cl:find-package \"CL-USER\")))\n"
                          "          (cl:load \"%2\" :verbose cl:nil :print cl:nil)\n")
                          .arg(tests.size())
                          .arg(escapeLispString(sourcePath));

    const QString functionSymbol = escapeLispString(functionName.toUpper());
    for (const auto &test : tests)
    {
        const QString expression =
            QStringLiteral("(cl:funcall (cl:symbol-function (cl:find-symbol \"%1\" \"CL-USER\")) %2)")
                .arg(functionSymbol, test.arguments);
        harness += QStringLiteral("          (check \"%1\" %2 (cl:lambda () %3))\n")
                       .arg(escapeLispString(test.name), test.expected, expression);
    }

    harness += QStringLiteral(
        "          (cl:format cl:*standard-output* \"RESULT:~D/~D~%\" passed total)\n"
        "          (sb-ext:exit :code (cl:if (cl:= passed total) 0 1)))\n"
        "      (cl:error (condition)\n"
        "        (cl:format cl:*standard-output* \"HARNESS_ERROR:~A~%\" condition)\n"
        "        (cl:format cl:*standard-output* \"RESULT:~D/~D~%\" passed total)\n"
        "        (sb-ext:exit :code 2)))))\n");
    return harness;
}
