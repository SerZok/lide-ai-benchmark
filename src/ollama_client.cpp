#include "ollama_client.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace
{
QJsonObject requestJson(const QString &url, const QByteArray &method,
                        const QJsonObject &payload, const QString &apiKey,
                        QString *error, qint64 timeoutMs = 300000)
{
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(static_cast<int>(timeoutMs));
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey.toUtf8());

    QNetworkReply *reply = method == QByteArrayLiteral("GET")
                               ? manager.get(request)
                               : manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(static_cast<int>(timeoutMs));
    loop.exec();
    if (!reply->isFinished())
    {
        reply->abort();
        if (error)
            *error = QStringLiteral("Request timed out after %1 ms.").arg(timeoutMs);
    }

    const QByteArray body = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (reply->error() != QNetworkReply::NoError)
    {
        if (error && error->isEmpty())
            *error = reply->errorString();
    }
    else if (!document.isObject())
    {
        if (error)
            *error = QStringLiteral("Server returned invalid JSON: %1").arg(parseError.errorString());
    }
    reply->deleteLater();
    return document.object();
}

QJsonObject parseStreamLine(const QByteArray &line)
{
    return QJsonDocument::fromJson(line).object();
}
}

OllamaClient::OllamaClient(const QString &endpoint, const QString &apiKey)
    : m_endpoint(endpoint.trimmed()), m_apiKey(apiKey)
{
    while (m_endpoint.endsWith(QLatin1Char('/')))
        m_endpoint.chop(1);
    if (m_endpoint.endsWith(QStringLiteral("/api/chat")))
        m_endpoint.chop(9);
    else if (m_endpoint.endsWith(QStringLiteral("/api/generate")))
        m_endpoint.chop(13);
}

QString OllamaClient::apiUrl(const QString &path) const
{
    return m_endpoint + QStringLiteral("/api/") + path;
}

QJsonObject OllamaClient::modelInfo(const QString &model, QString *error)
{
    if (error)
        error->clear();
    QString versionError;
    const QJsonObject server = requestJson(apiUrl(QStringLiteral("version")),
                                           QByteArrayLiteral("GET"), {}, m_apiKey,
                                           &versionError, 15000);
    QString modelError;
    const QJsonObject details = requestJson(
        apiUrl(QStringLiteral("show")), QByteArrayLiteral("POST"),
        QJsonObject{{"model", model}}, m_apiKey, &modelError, 30000);
    if (!modelError.isEmpty())
    {
        if (error)
            *error = modelError;
        return {};
    }
    const QJsonObject modelDetails = details.value("details").toObject();
    QString tagsError;
    const QJsonObject tags = requestJson(apiUrl(QStringLiteral("tags")),
                                         QByteArrayLiteral("GET"), {}, m_apiKey,
                                         &tagsError, 15000);
    QString digest;
    QString modifiedAt;
    for (const QJsonValue &value : tags.value("models").toArray())
    {
        const QJsonObject taggedModel = value.toObject();
        if (taggedModel.value("name").toString() == model)
        {
            digest = taggedModel.value("digest").toString();
            modifiedAt = taggedModel.value("modified_at").toString();
            break;
        }
    }
    QStringList metadataErrors;
    if (!versionError.isEmpty())
        metadataErrors.append(QStringLiteral("Ollama version: %1").arg(versionError));
    if (!tagsError.isEmpty())
        metadataErrors.append(QStringLiteral("Model digest: %1").arg(tagsError));
    if (error)
        *error = metadataErrors.join(QStringLiteral("; "));
    const QString modelVersion = !digest.isEmpty()
                                     ? digest
                                     : (!modifiedAt.isEmpty() ? modifiedAt : QStringLiteral("unknown"));
    return {
        {"version", modelVersion},
        {"versionSource", !digest.isEmpty()
                              ? QStringLiteral("digest")
                              : (!modifiedAt.isEmpty() ? QStringLiteral("modified_at")
                                                       : QStringLiteral("unknown"))},
        {"digest", digest},
        {"modifiedAt", modifiedAt},
        {"quantization", modelDetails.value("quantization_level").toString(QStringLiteral("unknown"))},
        {"family", modelDetails.value("family").toString()},
        {"parameterSize", modelDetails.value("parameter_size").toString()},
        {"serverVersion", server.value("version").toString(
                              versionError.isEmpty() ? QStringLiteral("unknown")
                                                     : QStringLiteral("unavailable"))}};
}

OllamaClient::Generation OllamaClient::generate(const QString &model, const QString &prompt,
                                                const QString &systemPrompt,
                                                const QJsonObject &options)
{
    Generation result;
    result.startedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(apiUrl(QStringLiteral("chat")))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(300000);
    if (!m_apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());

    QJsonArray messages;
    if (!systemPrompt.isEmpty())
        messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
    messages.append(QJsonObject{{"role", "user"}, {"content", prompt}});
    const QJsonObject payload{
        {"model", model}, {"stream", true}, {"messages", messages}, {"options", options}};
    QNetworkReply *reply =
        manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QByteArray pendingBytes;
    bool timedOut = false;
    bool streamCompleted = false;
    QElapsedTimer timer;
    timer.start();
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        pendingBytes += reply->readAll();
        while (true)
        {
            const qsizetype newline = pendingBytes.indexOf('\n');
            if (newline < 0)
                break;
            const QByteArray line = pendingBytes.left(newline).trimmed();
            pendingBytes.remove(0, newline + 1);
            if (line.isEmpty())
                continue;
            const QJsonObject event = parseStreamLine(line);
            const QJsonObject message = event.value("message").toObject();
            const QString token = message.value("content").toString();
            if (!token.isEmpty())
            {
                if (result.ttftMs < 0)
                    result.ttftMs = timer.elapsed();
                result.response += token;
            }
            if (event.value("done").toBool())
            {
                streamCompleted = true;
                result.reportedModel = event.value("model").toString();
                result.promptTokens = event.value("prompt_eval_count").toInteger(-1);
                result.completionTokens = event.value("eval_count").toInteger(-1);
                result.generationDurationNs = event.value("eval_duration").toInteger(-1);
            }
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(300000);
    loop.exec();
    pendingBytes += reply->readAll();
    if (!pendingBytes.trimmed().isEmpty())
    {
        const QJsonObject event = parseStreamLine(pendingBytes.trimmed());
        const QString token = event.value("message").toObject().value("content").toString();
        if (!token.isEmpty())
        {
            if (result.ttftMs < 0)
                result.ttftMs = timer.elapsed();
            result.response += token;
        }
        if (event.value("done").toBool())
        {
            streamCompleted = true;
            result.reportedModel = event.value("model").toString();
            result.promptTokens = event.value("prompt_eval_count").toInteger(-1);
            result.completionTokens = event.value("eval_count").toInteger(-1);
            result.generationDurationNs = event.value("eval_duration").toInteger(-1);
        }
    }
    result.elapsedMs = timer.elapsed();
    result.success = !timedOut && streamCompleted &&
                     reply->error() == QNetworkReply::NoError &&
                     !result.response.isEmpty();
    if (timedOut)
        result.error = QStringLiteral("Generation timed out after 300000 ms.");
    else if (reply->error() != QNetworkReply::NoError)
        result.error = reply->errorString();
    else if (!streamCompleted)
        result.error = QStringLiteral("The server ended the generation stream before completion.");
    else if (result.response.isEmpty())
        result.error = QStringLiteral("The server returned no generated content.");
    if (result.completionTokens > 0 && result.generationDurationNs > 0)
        result.tokensPerSecond =
            result.completionTokens * 1000000000.0 / result.generationDurationNs;
    else if (result.completionTokens > 0 && result.elapsedMs > result.ttftMs && result.ttftMs >= 0)
        result.tokensPerSecond =
            result.completionTokens * 1000.0 / (result.elapsedMs - result.ttftMs);
    reply->deleteLater();
    return result;
}
