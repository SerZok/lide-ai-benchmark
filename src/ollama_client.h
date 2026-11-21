#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

class OllamaClient
{
public:
    struct Generation
    {
        bool success = false;
        QString error;
        QString response;
        QString reportedModel;
        QString startedAtUtc;
        qint64 elapsedMs = 0;
        qint64 ttftMs = -1;
        qint64 promptTokens = -1;
        qint64 completionTokens = -1;
        qint64 generationDurationNs = -1;
        double tokensPerSecond = -1.0;
    };

    explicit OllamaClient(const QString &endpoint, const QString &apiKey = {});
    QStringList listModels(QString *error = nullptr);
    QJsonObject modelInfo(const QString &model, QString *error);
    Generation generate(const QString &model, const QString &prompt,
                        const QString &systemPrompt, const QJsonObject &options);

private:
    QString apiUrl(const QString &path) const;
    QString m_endpoint;
    QString m_apiKey;
};
