#include "harness_generator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QJsonObject task{
        {"function", "SUM-LIST"},
        {"tests", QJsonArray{
                      QJsonObject{{"name", "regular"}, {"arguments", "'(1 2 3)"},
                                  {"expected", "6"}}}}};
    QString error;
    const QString generated =
        HarnessGenerator::generate(task, QStringLiteral("C:\\temp\\candidate.lisp"), &error);
    if (!error.isEmpty() || !generated.contains(QStringLiteral("LIDE_RESULT:~D/~D")) ||
        !generated.contains(QStringLiteral("SUM-LIST")) ||
        !generated.contains(QStringLiteral("(cl:load \"C:\\\\temp\\\\candidate.lisp\"")))
    {
        qCritical() << "Harness generation failed:" << error << generated;
        return 1;
    }

    const QString invalid = HarnessGenerator::generate(
        QJsonObject{{"function", "BAD NAME"}, {"tests", QJsonArray{task.value("tests").toArray().first()} }},
        QStringLiteral("candidate.lisp"), &error);
    if (!invalid.isEmpty() || error.isEmpty())
    {
        qCritical() << "Invalid task was accepted.";
        return 1;
    }
    return 0;
}
