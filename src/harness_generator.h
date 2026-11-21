#pragma once

#include "sbcl_sandbox.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

class HarnessGenerator
{
public:
    static QString generate(const QJsonObject &task, const QString &sourcePath,
                            QString *error);
    static QString generate(const QString &sourcePath, const QString &functionName,
                            const QVector<SbclSandbox::TestCase> &tests);
};
