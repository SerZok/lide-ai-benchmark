#include "code_extractor.h"
#include "benchmark_window.h"
#include "ollama_client.h"
#include "sbcl_sandbox.h"

#include <QCommandLineParser>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QUrl>

#include <cstdlib>

namespace
{
    bool writeRecord(QFile &output, QJsonObject record)
    {
        record.insert(QStringLiteral("timestampUtc"),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        const QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
        if (output.write(line) != line.size())
            return false;
        output.flush();
        return output.error() == QFile::NoError;
    }

    bool readTasks(const QJsonArray &taskDefinitions, QVector<QJsonObject> *tasks, QString *error)
    {
        static const QRegularExpression functionNamePattern(
            QStringLiteral("^[A-Za-z0-9*+!$%&<=>?@^_~./:-]+$"));
        QSet<QString> taskIds;
        for (qsizetype index = 0; index < taskDefinitions.size(); ++index)
        {
            const QJsonObject task = taskDefinitions.at(index).toObject();
            const QString taskId = task.value("id").toString();
            const QString functionName = task.value("function").toString();
            const QJsonArray testDefinitions = task.value("tests").toArray();
            if (taskId.isEmpty() || taskIds.contains(taskId) ||
                task.value("prompt").toString().isEmpty() ||
                !functionNamePattern.match(functionName).hasMatch() || testDefinitions.isEmpty())
            {
                *error = QStringLiteral("Task %1 must define a unique id, prompt, a valid Lisp function name, and non-empty tests.")
                             .arg(index);
                return false;
            }
            taskIds.insert(taskId);
            for (qsizetype testIndex = 0; testIndex < testDefinitions.size(); ++testIndex)
            {
                const QJsonObject test = testDefinitions.at(testIndex).toObject();
                if (!test.value("arguments").isString() || test.value("arguments").toString().isEmpty() ||
                    !test.value("expected").isString() || test.value("expected").toString().isEmpty())
                {
                    *error = QStringLiteral("Task '%1' test %2 must have Lisp strings for arguments and expected.")
                                 .arg(taskId)
                                 .arg(testIndex);
                    return false;
                }
            }
            tasks->append(task);
        }
        return true;
    }

    QVector<SbclSandbox::TestCase> taskTests(const QJsonObject &task, QString *error)
    {
        QVector<SbclSandbox::TestCase> tests;
        for (const QJsonValue &value : task.value("tests").toArray())
        {
            const QJsonObject test = value.toObject();
            if (!test.value("arguments").isString() || !test.value("expected").isString())
            {
                *error = QStringLiteral("Each test requires string-valued Lisp expressions 'arguments' and 'expected'.");
                return {};
            }
            tests.append({test.value("name").toString(), test.value("arguments").toString(),
                          test.value("expected").toString()});
        }
        return tests;
    }

    QString errorCategory(const QString &status, const QString &sandboxOutput = {})
    {
        if (status == QStringLiteral("passed"))
            return QStringLiteral("none");
        if (status == QStringLiteral("compilation-error") || status == QStringLiteral("read-error"))
            return QStringLiteral("compilation_error");
        if (status == QStringLiteral("timeout") || status == QStringLiteral("memory-limit") ||
            status == QStringLiteral("output-limit"))
            return QStringLiteral("resource_violation");
        if (status == QStringLiteral("failed") &&
            sandboxOutput.contains(QStringLiteral("LIDE_TEST_ERROR:")))
            return QStringLiteral("runtime_error");
        if (status == QStringLiteral("failed"))
            return QStringLiteral("incorrect_result");
        if (status == QStringLiteral("no-code"))
            return QStringLiteral("extraction_error");
        if (status == QStringLiteral("failed") && sandboxOutput.contains(QStringLiteral("did not complete")))
            return QStringLiteral("resource_violation");
        return QStringLiteral("infrastructure_error");
    }

    QJsonObject generationObject(const OllamaClient::Generation &generation)
    {
        QJsonObject object{
            {"success", generation.success},
            {"startedAtUtc", generation.startedAtUtc},
            {"elapsedMs", generation.elapsedMs},
            {"ttftMs", generation.ttftMs},
            {"promptTokens", generation.promptTokens},
            {"completionTokens", generation.completionTokens},
            {"tokensPerSecond", generation.tokensPerSecond},
            {"reportedModel", generation.reportedModel}};
        if (!generation.success)
            object.insert("error", generation.error);
        return object;
    }

    int run(const QString &configPath, const QString &outputOverride,
            const QString &sbclOverride, const QString &endpointOverride)
    {
        QFile configFile(configPath);
        if (!configFile.open(QIODevice::ReadOnly))
        {
            QTextStream(stderr) << "Cannot open config: " << configFile.errorString() << '\n';
            return 2;
        }
        QJsonParseError parseError{};
        const QJsonDocument configDoc = QJsonDocument::fromJson(configFile.readAll(), &parseError);
        if (!configDoc.isObject())
        {
            QTextStream(stderr) << "Invalid config JSON: " << parseError.errorString() << '\n';
            return 2;
        }
        const QJsonObject config = configDoc.object();
        const QJsonArray models = config.value("models").toArray();
        const QJsonArray taskDefinitions = config.value("tasks").toArray();
        QVector<QJsonObject> validatedTasks;
        QString error;
        if (models.isEmpty() || taskDefinitions.isEmpty() ||
            !readTasks(taskDefinitions, &validatedTasks, &error))
        {
            QTextStream(stderr) << (error.isEmpty()
                                        ? "Config must contain non-empty models and tasks arrays."
                                        : error)
                                << '\n';
            return 2;
        }

        const int repetitions = config.value("repetitions").toInt(1);
        if (repetitions < 1 || repetitions > 10000)
        {
            QTextStream(stderr) << "repetitions must be between 1 and 10000.\n";
            return 2;
        }
        QSet<QString> modelNames;
        for (const QJsonValue &model : models)
        {
            if (!model.isString() || model.toString().trimmed().isEmpty() ||
                modelNames.contains(model.toString().trimmed()))
            {
                QTextStream(stderr) << "Each models entry must be a unique, non-empty model name string.\n";
                return 2;
            }
            modelNames.insert(model.toString().trimmed());
        }

        QString sbclPath = sbclOverride.isEmpty()
                               ? config.value("sbclPath").toString()
                               : sbclOverride;
        if (sbclPath.isEmpty())
        {
            QTextStream(stderr) << "Set sbclPath in the config or pass --sbcl.\n";
            return 2;
        }
        if (QDir::isRelativePath(sbclPath))
            sbclPath = QFileInfo(configPath).absoluteDir().absoluteFilePath(sbclPath);
        const QString endpoint = endpointOverride.isEmpty()
                                     ? config.value("endpoint").toString(QStringLiteral("http://127.0.0.1:11434"))
                                     : endpointOverride;
        const QUrl endpointUrl(endpoint);
        if (!endpointUrl.isValid() || (endpointUrl.scheme() != "http" && endpointUrl.scheme() != "https") ||
            endpointUrl.host().isEmpty())
        {
            QTextStream(stderr) << "endpoint must be a valid HTTP(S) URL.\n";
            return 2;
        }
        QString outputPath = outputOverride.isEmpty()
                                 ? config.value("output").toString(QStringLiteral("benchmark.jsonl"))
                                 : outputOverride;
        if (outputOverride.isEmpty() && QDir::isRelativePath(outputPath))
            outputPath = QFileInfo(configPath).absoluteDir().absoluteFilePath(outputPath);
        const QFileInfo outputInfo(outputPath);
        if (!QDir().mkpath(outputInfo.absolutePath()))
        {
            QTextStream(stderr) << "Cannot create output directory: " << outputInfo.absolutePath() << '\n';
            return 2;
        }
        QFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        {
            QTextStream(stderr) << "Cannot open JSONL output: " << output.errorString() << '\n';
            return 2;
        }

        const QString apiKey = QString::fromLocal8Bit(qgetenv("OLLAMA_API_KEY"));
        OllamaClient client(endpoint, apiKey);
        const QString systemPrompt = config.value("systemPrompt").toString();
        QJsonObject options = config.value("options").toObject();
        if (!options.contains("temperature"))
            options.insert("temperature", 0.2);
        if (!options.contains("num_predict"))
            options.insert("num_predict", 512);

        QTextStream out(stdout);
        int totalRuns = 0;
        for (const QJsonValue &modelValue : models)
        {
            const QString model = modelValue.toString();
            QString metadataError;
            const QJsonObject metadata = client.modelInfo(model, &metadataError);
            for (int attempt = 1; attempt <= repetitions; ++attempt)
            {
                for (const QJsonObject &task : validatedTasks)
                {
                    const QString taskId = task.value("id").toString();
                    const OllamaClient::Generation generation =
                        client.generate(model, task.value("prompt").toString(), systemPrompt, options);
                    QJsonObject record{
                        {"recordType", "result"},
                        {"model", model},
                        {"modelInfo", metadata},
                        {"modelInfoError", metadataError},
                        {"taskId", taskId},
                        {"attempt", attempt},
                        {"prompt", task.value("prompt").toString()},
                        {"generationConfig",
                         QJsonObject{{"api", "ollama-api-chat-stream"},
                                     {"systemPrompt", systemPrompt},
                                     {"options", options}}},
                        {"generation", generationObject(generation)}};

                    QString status;
                    QString code;
                    QJsonObject sandboxResult;
                    if (!generation.success)
                    {
                        status = QStringLiteral("generation-error");
                    }
                    else
                    {
                        code = CodeExtractor::extract(generation.response);
                        if (code.isEmpty())
                            status = QStringLiteral("no-code");
                        else
                        {
                            const QVector<SbclSandbox::TestCase> tests = taskTests(task, &error);
                            sandboxResult = SbclSandbox::run(
                                sbclPath, code, task.value("function").toString(), tests);
                            status = sandboxResult.value("status").toString(QStringLiteral("failed"));
                        }
                    }
                    record.insert("response", generation.response);
                    record.insert("extractedCode", code);
                    record.insert("sandbox", sandboxResult);
                    record.insert("status", status);
                    record.insert("errorCategory",
                                  status == QStringLiteral("generation-error")
                                      ? QStringLiteral("generation_error")
                                      : errorCategory(status, sandboxResult.value("output").toString()));
                    if (!writeRecord(output, record))
                    {
                        QTextStream(stderr) << "Failed to write JSONL result: "
                                            << output.errorString() << '\n';
                        return 2;
                    }
                    ++totalRuns;
                    out << model << " / " << taskId << " / attempt " << attempt
                        << ": " << status << '\n';
                    out.flush();
                }
            }
        }
        out << "Completed " << totalRuns << " runs. JSONL: " << output.fileName() << '\n';
        return 0;
    }
}

int main(int argc, char *argv[])
{
    for (int index = 1; index < argc; ++index)
    {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--gui"))
        {
            QApplication app(argc, argv);
            QCoreApplication::setApplicationName(QStringLiteral("lide-ai-benchmark"));
            BenchmarkWindow window;
            window.show();
            return app.exec();
        }
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("lide-ai-benchmark"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Common Lisp model benchmark runner"));
    parser.addHelpOption();
    QCommandLineOption guiOption(QStringLiteral("gui"),
                                 QStringLiteral("GUI mode."));
    parser.addOption(guiOption);
    QCommandLineOption configOption({QStringLiteral("c"), QStringLiteral("config")},
                                    QStringLiteral("Benchmark JSON configuration file."),
                                    QStringLiteral("file"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                    QStringLiteral("JSONL output path."),
                                    QStringLiteral("file"));
    QCommandLineOption sbclOption(QStringLiteral("sbcl"), QStringLiteral("SBCL executable path."),
                                  QStringLiteral("path"));
    QCommandLineOption endpointOption(QStringLiteral("endpoint"),
                                      QStringLiteral("Ollama server URL."),
                                      QStringLiteral("url"));
    parser.addOption(configOption);
    parser.addOption(outputOption);
    parser.addOption(sbclOption);
    parser.addOption(endpointOption);
    parser.process(app);
    if (!parser.isSet(configOption))
    {
        QTextStream(stderr) << "Pass --config <file>. Use --help for options.\n";
        return 2;
    }
    return run(parser.value(configOption), parser.value(outputOption),
               parser.value(sbclOption), parser.value(endpointOption));
}
