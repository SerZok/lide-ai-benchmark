#include "benchmark_window.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

BenchmarkWindow::BenchmarkWindow(QWidget *parent)
    : QMainWindow(parent),
      m_configPath(new QLineEdit(this)),
      m_endpoint(new QLineEdit(this)),
      m_models(new QLineEdit(this)),
      m_repetitions(new QSpinBox(this)),
      m_sbclPath(new QLineEdit(this)),
      m_apiKey(new QLineEdit(this)),
      m_outputPath(new QLineEdit(this)),
      m_output(new QPlainTextEdit(this)),
      m_runButton(new QPushButton(tr("Запустить"), this)),
      m_cancelButton(new QPushButton(tr("Остановить"), this)),
      m_aggregateButton(new QPushButton(tr("Агрегировать JSONL..."), this))
{
    setWindowTitle(tr("LIDE — бенчмарк моделей"));
    resize(900, 680);
    m_repetitions->setRange(1, 10000);
    m_repetitions->setValue(1);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(tr("Необязательно; не сохраняется в файле конфигурации"));
    m_apiKey->setText(qEnvironmentVariable("OLLAMA_API_KEY"));
    m_endpoint->setPlaceholderText(QStringLiteral("http://127.0.0.1:11434"));
    m_models->setPlaceholderText(tr("Модели через запятую, например qwen2.5-coder:7b"));
    m_sbclPath->setPlaceholderText(tr("Путь к sbcl.exe"));
    m_outputPath->setPlaceholderText(tr("Путь к JSONL-отчёту"));
    m_output->setReadOnly(true);
    m_cancelButton->setEnabled(false);

    auto *configRow = new QWidget(this);
    auto *configLayout = new QHBoxLayout(configRow);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->addWidget(m_configPath, 1);
    auto *configBrowse = new QPushButton(tr("Обзор..."), configRow);
    configLayout->addWidget(configBrowse);

    auto *sbclRow = new QWidget(this);
    auto *sbclLayout = new QHBoxLayout(sbclRow);
    sbclLayout->setContentsMargins(0, 0, 0, 0);
    sbclLayout->addWidget(m_sbclPath, 1);
    auto *sbclBrowse = new QPushButton(tr("Обзор..."), sbclRow);
    sbclLayout->addWidget(sbclBrowse);

    auto *outputRow = new QWidget(this);
    auto *outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->addWidget(m_outputPath, 1);
    auto *outputBrowse = new QPushButton(tr("Обзор..."), outputRow);
    outputLayout->addWidget(outputBrowse);

    auto *form = new QFormLayout();
    form->addRow(tr("Набор задач (JSON):"), configRow);
    form->addRow(tr("Адрес Ollama:"), m_endpoint);
    form->addRow(tr("Модели:"), m_models);
    form->addRow(tr("Повторов на задачу:"), m_repetitions);
    form->addRow(tr("SBCL:"), sbclRow);
    form->addRow(tr("API-ключ:"), m_apiKey);
    form->addRow(tr("Результаты JSONL:"), outputRow);

    auto *buttons = new QHBoxLayout();
    buttons->addWidget(m_runButton);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_aggregateButton);
    buttons->addStretch();

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(m_output, 1);
    setCentralWidget(central);

    connect(configBrowse, &QPushButton::clicked, this, &BenchmarkWindow::browseConfig);
    connect(sbclBrowse, &QPushButton::clicked, this, &BenchmarkWindow::browseSbcl);
    connect(outputBrowse, &QPushButton::clicked, this, &BenchmarkWindow::browseOutput);
    connect(m_runButton, &QPushButton::clicked, this, &BenchmarkWindow::startBenchmark);
    connect(m_cancelButton, &QPushButton::clicked, this, &BenchmarkWindow::cancelProcess);
    connect(m_aggregateButton, &QPushButton::clicked, this, &BenchmarkWindow::aggregateResults);

    const QString examplePath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("benchmark.example.json"));
    if (QFileInfo::exists(examplePath))
        loadConfig(examplePath);
    const QString outputDirectory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("benchmark"));
    const QString outputFile = QStringLiteral("benchmark_%1.jsonl")
                                   .arg(QDateTime::currentDateTimeUtc()
                                            .toString(QStringLiteral("yyyyMMdd'T'HHmmsszzz'Z'")));
    m_outputPath->setText(QDir(outputDirectory).filePath(outputFile));
    const QString configuredEndpoint = qEnvironmentVariable("LIDE_AI_ENDPOINT");
    if (!configuredEndpoint.isEmpty())
        m_endpoint->setText(configuredEndpoint);
    const QString configuredModel = qEnvironmentVariable("LIDE_AI_MODEL");
    if (!configuredModel.isEmpty())
        m_models->setText(configuredModel);
    const QString configuredSbcl = qEnvironmentVariable("LIDE_SBCL_PATH");
    if (!configuredSbcl.isEmpty())
        m_sbclPath->setText(configuredSbcl);
    const QString bundledSbcl =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("sbcl/sbcl.exe"));
    if (QFileInfo::exists(bundledSbcl))
        m_sbclPath->setText(bundledSbcl);
}

void BenchmarkWindow::browseConfig()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Выберите конфигурацию бенчмарка"), m_configPath->text(),
        tr("JSON-файлы (*.json);;Все файлы (*)"));
    if (!path.isEmpty())
        loadConfig(path);
}

void BenchmarkWindow::loadConfig(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Ошибка конфигурации"), file.errorString());
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject())
    {
        QMessageBox::warning(this, tr("Ошибка конфигурации"),
                             tr("Некорректный JSON: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject config = document.object();
    m_configPath->setText(QFileInfo(path).absoluteFilePath());
    m_endpoint->setText(config.value("endpoint").toString(QStringLiteral("http://127.0.0.1:11434")));
    QStringList modelNames;
    for (const QJsonValue &model : config.value("models").toArray())
        modelNames.append(model.toString());
    m_models->setText(modelNames.join(QStringLiteral(", ")));
    m_repetitions->setValue(qBound(1, config.value("repetitions").toInt(1), 10000));

    QString sbcl = config.value("sbclPath").toString();
    if (!sbcl.isEmpty() && QDir::isRelativePath(sbcl))
        sbcl = QFileInfo(path).absoluteDir().absoluteFilePath(sbcl);
    m_sbclPath->setText(sbcl);

    QString output = config.value("output").toString(
        QFileInfo(path).absoluteDir().filePath(QStringLiteral("results.jsonl")));
    if (QDir::isRelativePath(output))
        output = QFileInfo(path).absoluteDir().absoluteFilePath(output);
    m_outputPath->setText(QDir::cleanPath(output));
    statusBar()->showMessage(tr("Конфигурация загружена: %1").arg(QFileInfo(path).fileName()));
}

void BenchmarkWindow::browseSbcl()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Выберите SBCL"), m_sbclPath->text(), tr("SBCL (sbcl.exe);;Все файлы (*)"));
    if (!path.isEmpty())
        m_sbclPath->setText(path);
}

void BenchmarkWindow::browseOutput()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Сохранить результаты JSONL"), m_outputPath->text(),
        tr("JSON Lines (*.jsonl);;Все файлы (*)"));
    if (!path.isEmpty())
        m_outputPath->setText(path);
}

void BenchmarkWindow::startBenchmark()
{
    if (m_process)
        return;
    if (m_configPath->text().trimmed().isEmpty() ||
        m_sbclPath->text().trimmed().isEmpty() ||
        m_outputPath->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, tr("Не хватает данных"),
                             tr("Укажите конфигурацию, SBCL и файл результатов."));
        return;
    }

    QFile configFile(m_configPath->text());
    if (!configFile.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Ошибка конфигурации"), configFile.errorString());
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(configFile.readAll(), &parseError);
    if (!document.isObject())
    {
        QMessageBox::warning(this, tr("Ошибка конфигурации"),
                             tr("Некорректный JSON: %1").arg(parseError.errorString()));
        return;
    }

    QStringList models;
    for (const QString &model : m_models->text().split(
             QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts))
        models.append(model.trimmed());
    if (models.isEmpty())
    {
        QMessageBox::warning(this, tr("Не выбрана модель"), tr("Укажите хотя бы одну модель."));
        return;
    }

    QJsonObject config = document.object();
    config.insert("endpoint", m_endpoint->text().trimmed());
    config.insert("models", QJsonArray::fromStringList(models));
    config.insert("repetitions", m_repetitions->value());
    config.insert("sbclPath", QFileInfo(m_sbclPath->text()).absoluteFilePath());
    config.insert("output", QFileInfo(m_outputPath->text()).absoluteFilePath());

    const QJsonArray tasks = config.value("tasks").toArray();
    if (tasks.isEmpty())
    {
        QMessageBox::warning(this, tr("Нет задач"),
                             tr("В выбранной конфигурации нет массива tasks."));
        return;
    }
    if (!m_temporaryDirectory.isValid())
    {
        QMessageBox::critical(this, tr("Ошибка"), tr("Не удалось создать временную конфигурацию."));
        return;
    }

    const QString temporaryConfigPath =
        QDir(m_temporaryDirectory.path()).filePath(QStringLiteral("run.json"));
    QFile temporaryConfig(temporaryConfigPath);
    if (!temporaryConfig.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::critical(this, tr("Ошибка"), temporaryConfig.errorString());
        return;
    }
    const QByteArray configData = QJsonDocument(config).toJson(QJsonDocument::Indented);
    if (temporaryConfig.write(configData) != configData.size())
    {
        QMessageBox::critical(this, tr("Ошибка"), temporaryConfig.errorString());
        return;
    }
    temporaryConfig.close();

    m_output->clear();
    m_output->appendPlainText(tr("Запуск %1 моделей, %2 задач, %3 повторов.")
                                  .arg(models.size())
                                  .arg(tasks.size())
                                  .arg(m_repetitions->value()));
    m_process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (m_apiKey->text().isEmpty())
        environment.remove(QStringLiteral("OLLAMA_API_KEY"));
    else
        environment.insert(QStringLiteral("OLLAMA_API_KEY"), m_apiKey->text());
    m_process->setProcessEnvironment(environment);
    m_process->setProgram(QCoreApplication::applicationFilePath());
    m_process->setArguments({QStringLiteral("--config"), temporaryConfigPath});
    m_process->setWorkingDirectory(QCoreApplication::applicationDirPath());
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &BenchmarkWindow::appendProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &BenchmarkWindow::appendProcessOutput);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            m_output->appendPlainText(tr("Не удалось запустить CLI: %1").arg(m_process->errorString()));
            setRunning(false);
            m_process->deleteLater();
            m_process = nullptr;
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BenchmarkWindow::finishProcess);
    setRunning(true);
    m_process->start();
}

void BenchmarkWindow::aggregateResults()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Выберите JSONL-отчёт"), m_outputPath->text(),
        tr("JSON Lines (*.jsonl);;Все файлы (*)"));
    if (path.isEmpty())
        return;
    const QString scriptPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("aggregator.py"));
    if (!QFileInfo::exists(scriptPath))
    {
        QMessageBox::warning(this, tr("Агрегатор не найден"),
                             tr("Рядом с программой отсутствует aggregator.py."));
        return;
    }
    if (m_process)
        return;

    m_output->appendPlainText(tr("Агрегация результатов: %1").arg(path));
    m_process = new QProcess(this);
#ifdef Q_OS_WIN
    m_process->setProgram(QStringLiteral("python"));
#else
    m_process->setProgram(QStringLiteral("python3"));
#endif
    m_process->setArguments({scriptPath, path});
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &BenchmarkWindow::appendProcessOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &BenchmarkWindow::appendProcessOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BenchmarkWindow::finishProcess);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
        {
            m_output->appendPlainText(tr("Не удалось запустить Python: %1").arg(m_process->errorString()));
            setRunning(false);
            m_process->deleteLater();
            m_process = nullptr;
        }
    });
    setRunning(true);
    m_process->start();
}

void BenchmarkWindow::cancelProcess()
{
    if (!m_process)
        return;
    m_output->appendPlainText(tr("Остановка процесса..."));
    m_process->terminate();
    QTimer::singleShot(3000, m_process, [process = m_process]() {
        if (process->state() != QProcess::NotRunning)
            process->kill();
    });
}

void BenchmarkWindow::appendProcessOutput()
{
    if (m_process)
    {
        m_output->appendPlainText(QString::fromLocal8Bit(m_process->readAllStandardOutput()));
        m_output->appendPlainText(QString::fromLocal8Bit(m_process->readAllStandardError()));
    }
}

void BenchmarkWindow::finishProcess(int exitCode, QProcess::ExitStatus exitStatus)
{
    appendProcessOutput();
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
    m_output->appendPlainText(success
                                  ? tr("Процесс завершился успешно.")
                                  : tr("Процесс завершился с ошибкой (код %1).").arg(exitCode));
    setRunning(false);
    m_process->deleteLater();
    m_process = nullptr;
}

void BenchmarkWindow::setRunning(bool running)
{
    m_runButton->setEnabled(!running);
    m_aggregateButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
    m_configPath->setEnabled(!running);
}

void BenchmarkWindow::closeEvent(QCloseEvent *event)
{
    if (m_process && m_process->state() != QProcess::NotRunning)
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Бенчмарк выполняется"),
            tr("Закрытие окна остановит текущую операцию. Продолжить?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    QMainWindow::closeEvent(event);
}
