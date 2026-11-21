#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>

class SbclSandbox
{
public:
    struct TestCase
    {
        QString name;
        QString arguments;
        QString expected;
    };

    static QJsonObject run(const QString &sbclExecutable, const QString &sourceCode,
                           const QString &functionName, const QVector<TestCase> &tests);
};
