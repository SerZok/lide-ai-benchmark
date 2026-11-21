#include "benchmark_window.h"
#include "ollama_client.h"

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
#include <QHeaderView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QColor>
#include <QStringList>
#include <QComboBox>
#include <QStandardItemModel>
#include <QListView>
#include <QMouseEvent>

namespace
{
    QString resultStatusText(const QString &status)
    {
        if (status == QStringLiteral("passed"))
            return QObject::tr("Пройдено");
        if (status == QStringLiteral("failed"))
            return QObject::tr("Не пройдено");
        if (status == QStringLiteral("compilation-error"))
            return QObject::tr("Ошибка компиляции");
        if (status == QStringLiteral("timeout"))
            return QObject::tr("Таймаут");
        if (status == QStringLiteral("generation-error"))
            return QObject::tr("Ошибка генерации");
        if (status == QStringLiteral("no-code"))
            return QObject::tr("Код не найден");
        if (status == QStringLiteral("memory-limit"))
            return QObject::tr("Лимит памяти");
        if (status == QStringLiteral("output-limit"))
            return QObject::tr("Лимит вывода");
        if (status == QStringLiteral("unavailable"))
            return QObject::tr("SBCL недоступен");
        return status.isEmpty() ? QObject::tr("Выполняется") : status;
    }

    QColor resultStatusColor(const QString &status)
    {
        if (status == QStringLiteral("passed"))
            return QColor(QStringLiteral("#2e8b57"));
        if (status == QStringLiteral("failed") || status == QStringLiteral("compilation-error") ||
            status == QStringLiteral("generation-error") || status == QStringLiteral("no-code"))
            return QColor(QStringLiteral("#c0392b"));
        if (status == QStringLiteral("timeout") || status == QStringLiteral("memory-limit") ||
            status == QStringLiteral("output-limit"))
            return QColor(QStringLiteral("#d68910"));
        return QColor(QStringLiteral("#607d8b"));
    }
}

BenchmarkWindow::BenchmarkWindow(QWidget *parent)
    : QMainWindow(parent),
      m_configPath(new QLineEdit(this)),
      m_endpoint(new QLineEdit(this)),
      m_models(new QComboBox(this)),
      m_modelsModel(new QStandardItemModel(m_models)),
      m_repetitions(new QSpinBox(this)),
      m_sbclPath(new QLineEdit(this)),
      m_apiKey(new QLineEdit(this)),
      m_outputPath(new QLineEdit(this)),
      m_output(new QPlainTextEdit(this)),
      m_resultDetails(new QPlainTextEdit(this)),
      m_results(new QTableWidget(this)),
      m_summary(new QLabel(this)),
      m_runButton(new QPushButton(tr("Запустить"), this)),
      m_cancelButton(new QPushButton(tr("Остановить"), this)),
      m_aggregateButton(new QPushButton(tr("Экспорт CSV/Markdown..."), this)),
      m_loadResultsButton(new QPushButton(tr("Открыть JSONL..."), this)),
      m_refreshModels(nullptr)
{
    m_models->setModel(m_modelsModel);
    m_models->setEditable(true);
    m_models->setCompleter(nullptr);
    m_models->setPlaceholderText(tr("Нажмите для выбора моделей"));
    m_models->view()->viewport()->installEventFilter(this);
    m_refreshModels = new QPushButton(tr("Обновить"), this);
    setWindowTitle(tr("LIDE — бенчмарк моделей"));
    resize(900, 680);
    m_repetitions->setRange(1, 10000);
    m_repetitions->setValue(1);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(tr("Необязательно; не сохраняется в файле конфигурации"));
    m_apiKey->setText(qEnvironmentVariable("OLLAMA_API_KEY"));
    m_endpoint->setPlaceholderText(QStringLiteral("http://127.0.0.1:11434"));
    m_models->setPlaceholderText(tr("Нажмите «Обновить» для загрузки списка моделей"));
    m_sbclPath->setPlaceholderText(tr("Путь к sbcl.exe"));
    m_outputPath->setPlaceholderText(tr("Путь к JSONL-отчёту"));
    m_output->setReadOnly(true);
    m_resultDetails->setReadOnly(true);
    m_resultDetails->setPlaceholderText(tr("Выберите строку, чтобы посмотреть ответ модели и результат SBCL."));
    m_cancelButton->setEnabled(false);
    m_results->setColumnCount(8);
    m_results->setHorizontalHeaderLabels(
        {tr("Модель"), tr("Задача"), tr("Попытка"), tr("Результат"),
         tr("Тесты"), tr("Время, с"), tr("TTFT, мс"), tr("Токенов/с")});
    m_results->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_results->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_results->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_results->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_results->verticalHeader()->setVisible(false);
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setAlternatingRowColors(true);
    m_results->setSortingEnabled(true);
    m_summary->setObjectName(QStringLiteral("benchmarkSummary"));
    m_summary->setText(tr("Результаты появятся здесь после запуска или загрузки JSONL."));

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
    {
        auto *modelsRow = new QWidget(this);
        auto *modelsLayout = new QHBoxLayout(modelsRow);
        modelsLayout->setContentsMargins(0, 0, 0, 0);
        modelsLayout->addWidget(m_models, 1);
        modelsLayout->addWidget(m_refreshModels);
        form->addRow(tr("Модели:"), modelsRow);
    }
    form->addRow(tr("Повторов на задачу:"), m_repetitions);
    form->addRow(tr("SBCL:"), sbclRow);
    form->addRow(tr("API-ключ:"), m_apiKey);
    form->addRow(tr("Результаты JSONL:"), outputRow);

    auto *buttons = new QHBoxLayout();
    buttons->addWidget(m_runButton);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_loadResultsButton);
    buttons->addWidget(m_aggregateButton);
    buttons->addStretch();

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addWidget(m_summary);
    auto *resultsSplitter = new QSplitter(Qt::Vertical, central);
    resultsSplitter->addWidget(m_results);
    resultsSplitter->addWidget(m_resultDetails);
    resultsSplitter->setStretchFactor(0, 3);
    resultsSplitter->setStretchFactor(1, 2);
    resultsSplitter->setChildrenCollapsible(false);
    layout->addWidget(resultsSplitter, 1);
    m_output->setMaximumHeight(100);
    layout->addWidget(m_output);
    setCentralWidget(central);

    connect(configBrowse, &QPushButton::clicked, this, &BenchmarkWindow::browseConfig);
    connect(sbclBrowse, &QPushButton::clicked, this, &BenchmarkWindow::browseSbcl);
    connect(outputBrowse, &QPushButton::clicked, this, &BenchmarkWindow::browseOutput);
    connect(m_refreshModels, &QPushButton::clicked, this, &BenchmarkWindow::refreshModels);
    connect(m_runButton, &QPushButton::clicked, this, &BenchmarkWindow::startBenchmark);
    connect(m_cancelButton, &QPushButton::clicked, this, &BenchmarkWindow::cancelProcess);
    connect(m_aggregateButton, &QPushButton::clicked, this, &BenchmarkWindow::aggregateResults);
    connect(m_loadResultsButton, &QPushButton::clicked, this, &BenchmarkWindow::loadResults);
    connect(m_results, &QTableWidget::cellClicked, this, &BenchmarkWindow::showSelectedResult);
    connect(&m_resultsTimer, &QTimer::timeout, this, &BenchmarkWindow::pollResults);
    m_resultsTimer.setInterval(500);

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
    m_resultsTimer.start();
    const QString configuredEndpoint = qEnvironmentVariable("LIDE_AI_ENDPOINT");
    if (!configuredEndpoint.isEmpty())
        m_endpoint->setText(configuredEndpoint);
    const QString configuredModel = qEnvironmentVariable("LIDE_AI_MODEL");
    if (!configuredModel.isEmpty())
        m_models->addItem(configuredModel);
    const QString configuredSbcl = qEnvironmentVariable("LIDE_SBCL_PATH");
    if (!configuredSbcl.isEmpty())
        m_sbclPath->setText(configuredSbcl);
    const QString bundledSbcl =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("sbcl/sbcl.exe"));
    if (QFileInfo::exists(bundledSbcl))
        m_sbclPath->setText(bundledSbcl);
}

void BenchmarkWindow::refreshModels()
{
    const QString endpoint = m_endpoint->text().trimmed();
    if (endpoint.isEmpty())
    {
        m_output->appendPlainText(tr("Укажите адрес Ollama для получения списка моделей."));
        return;
    }
    m_refreshModels->setEnabled(false);
    m_modelsModel->clear();
    m_output->appendPlainText(tr("Запрос списка моделей из %1...").arg(endpoint));

    const QString apiKey = m_apiKey->text();
    OllamaClient client(endpoint, apiKey);
    QString error;
    const QStringList models = client.listModels(&error);
    if (!error.isEmpty())
    {
        m_output->appendPlainText(tr("Ошибка: %1").arg(error));
    }
    else if (models.isEmpty())
    {
        m_output->appendPlainText(tr("Список моделей пуст."));
    }
    else
    {
        for (const QString &name : models)
        {
            auto *item = new QStandardItem(name);
            item->setCheckable(true);
            item->setCheckState(Qt::Unchecked);
            m_modelsModel->appendRow(item);
        }
        updateModelsDisplayText();
        m_output->appendPlainText(tr("Загружено %1 модель(ей).").arg(models.size()));
    }
    m_refreshModels->setEnabled(true);
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
    for (const QJsonValue &model : config.value("models").toArray())
    {
        auto *item = new QStandardItem(model.toString());
        item->setCheckable(true);
        item->setCheckState(Qt::Checked);
        m_modelsModel->appendRow(item);
    }
    updateModelsDisplayText();
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
    m_jsonlPosition = 0;
    m_pendingJsonl.clear();
    m_records.clear();
    m_results->setRowCount(0);
    m_resultDetails->clear();
    updateSummary();
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
    for (int i = 0; i < m_modelsModel->rowCount(); ++i)
    {
        auto *item = m_modelsModel->item(i);
        if (item && item->checkState() == Qt::Checked)
            models.append(item->text().trimmed());
    }
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
    m_records.clear();
    m_results->setRowCount(0);
    m_resultDetails->clear();
    m_jsonlPosition = 0;
    m_pendingJsonl.clear();
    updateSummary();
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
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
            {
        if (error == QProcess::FailedToStart)
        {
            m_output->appendPlainText(tr("Не удалось запустить CLI: %1").arg(m_process->errorString()));
            setRunning(false);
            m_process->deleteLater();
            m_process = nullptr;
        } });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &BenchmarkWindow::finishProcess);
    setRunning(true);
    m_process->start();
    QTimer::singleShot(100, this, &BenchmarkWindow::pollResults);
}

void BenchmarkWindow::loadResults()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Открыть результаты бенчмарка"), m_outputPath->text(),
        tr("JSON Lines (*.jsonl);;Все файлы (*)"));
    if (path.isEmpty())
        return;
    m_outputPath->setText(QFileInfo(path).absoluteFilePath());
    m_records.clear();
    m_results->setRowCount(0);
    m_resultDetails->clear();
    m_jsonlPosition = 0;
    m_pendingJsonl.clear();
    pollResults();
    statusBar()->showMessage(tr("Загружен отчёт: %1").arg(QFileInfo(path).fileName()));
}

void BenchmarkWindow::pollResults()
{
    const QString path = m_outputPath->text().trimmed();
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    if (file.size() < m_jsonlPosition)
    {
        m_jsonlPosition = 0;
        m_pendingJsonl.clear();
        m_records.clear();
        m_results->setRowCount(0);
        m_resultDetails->clear();
    }
    if (!file.seek(m_jsonlPosition))
    {
        m_output->appendPlainText(tr("Не удалось прочитать JSONL: %1").arg(file.errorString()));
        return;
    }

    const QByteArray added = file.readAll();
    m_jsonlPosition += added.size();
    m_pendingJsonl += added;
    while (true)
    {
        const qsizetype newline = m_pendingJsonl.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = m_pendingJsonl.left(newline).trimmed();
        m_pendingJsonl.remove(0, newline + 1);
        if (!line.isEmpty())
            appendResultLine(line);
    }
}

void BenchmarkWindow::appendResultLine(const QByteArray &line)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (!document.isObject())
    {
        m_output->appendPlainText(tr("Пропущена строка JSONL: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject record = document.object();
    if (record.value("recordType").toString() != QStringLiteral("result"))
        return;

    m_records.append(record);
    const QJsonObject generation = record.value("generation").toObject();
    const QJsonObject sandbox = record.value("sandbox").toObject();
    const int row = m_results->rowCount();
    m_results->setSortingEnabled(false);
    m_results->insertRow(row);

    const QString status = record.value("status").toString();
    const int passed = sandbox.value("passedTests").toInt(-1);
    const int total = sandbox.value("totalTests").toInt(-1);
    const QString tests = total >= 0
                              ? QStringLiteral("%1/%2").arg(passed).arg(total)
                              : QStringLiteral("—");
    const qint64 elapsed = generation.value("elapsedMs").toInteger(-1);
    const qint64 ttft = generation.value("ttftMs").toInteger(-1);
    const double speed = generation.value("tokensPerSecond").toDouble(-1.0);
    const QStringList values{
        record.value("model").toString(),
        record.value("taskId").toString(),
        QString::number(record.value("attempt").toInt(1)),
        resultStatusText(status),
        tests,
        elapsed >= 0 ? QString::number(elapsed / 1000.0, 'f', 2) : QStringLiteral("—"),
        ttft >= 0 ? QString::number(ttft) : QStringLiteral("—"),
        speed >= 0.0 ? QString::number(speed, 'f', 1) : QStringLiteral("—")};

    for (int column = 0; column < values.size(); ++column)
    {
        auto *item = new QTableWidgetItem(values.at(column));
        item->setData(Qt::UserRole, m_records.size() - 1);
        if (column == 3)
        {
            item->setForeground(Qt::white);
            item->setBackground(resultStatusColor(status));
            item->setTextAlignment(Qt::AlignCenter);
        }
        else if (column >= 2)
            item->setTextAlignment(Qt::AlignCenter);
        m_results->setItem(row, column, item);
    }
    m_results->setSortingEnabled(true);
    updateSummary();
    if (m_results->rowCount() == 1)
    {
        m_results->selectRow(0);
        showSelectedResult(0, 0);
    }
}

void BenchmarkWindow::showSelectedResult(int row, int)
{
    if (row < 0 || !m_results->item(row, 0))
        return;
    const int recordIndex = m_results->item(row, 0)->data(Qt::UserRole).toInt();
    if (recordIndex < 0 || recordIndex >= m_records.size())
        return;

    const QJsonObject record = m_records.at(recordIndex);
    const QJsonObject sandbox = record.value("sandbox").toObject();
    QStringList details{
        tr("Модель: %1").arg(record.value("model").toString()),
        tr("Задача: %1, попытка %2").arg(record.value("taskId").toString()).arg(record.value("attempt").toInt(1)),
        tr("Результат: %1 (%2)")
            .arg(resultStatusText(record.value("status").toString()),
                 record.value("errorCategory").toString()),
        QString(),
        tr("Ответ модели:"),
        record.value("response").toString(),
        QString(),
        tr("Извлечённый Lisp-код:"),
        record.value("extractedCode").toString(),
        QString(),
        tr("Результат SBCL:"),
        sandbox.value("output").toString(
            sandbox.value("message").toString(sandbox.value("status").toString()))};
    m_resultDetails->setPlainText(details.join(QLatin1Char('\n')));
}

void BenchmarkWindow::updateSummary()
{
    QSet<QString> firstAttemptTasks;
    int passed = 0;
    int compileErrors = 0;
    int resourceViolations = 0;
    for (const QJsonObject &record : m_records)
    {
        if (record.value("attempt").toInt(1) != 1)
            continue;
        const QString key = record.value("model").toString() + QLatin1Char('#') +
                            record.value("taskId").toString();
        if (firstAttemptTasks.contains(key))
            continue;
        firstAttemptTasks.insert(key);
        passed += record.value("status").toString() == QStringLiteral("passed") ? 1 : 0;
        const QString category = record.value("errorCategory").toString();
        compileErrors += category == QStringLiteral("compilation_error") ? 1 : 0;
        resourceViolations += category == QStringLiteral("resource_violation") ? 1 : 0;
    }
    const int count = firstAttemptTasks.size();
    m_summary->setText(
        tr("Записей: %1  ·  pass@1: %2/%3 (%4%)  ·  CER: %5%  ·  RVR: %6%")
            .arg(m_records.size())
            .arg(passed)
            .arg(count)
            .arg(count ? QString::number(passed * 100.0 / count, 'f', 1) : QStringLiteral("0.0"))
            .arg(count ? QString::number(compileErrors * 100.0 / count, 'f', 1)
                       : QStringLiteral("0.0"))
            .arg(count ? QString::number(resourceViolations * 100.0 / count, 'f', 1)
                       : QStringLiteral("0.0")));
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
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
            {
        if (error == QProcess::FailedToStart)
        {
            m_output->appendPlainText(tr("Не удалось запустить Python: %1").arg(m_process->errorString()));
            setRunning(false);
            m_process->deleteLater();
            m_process = nullptr;
        } });
    setRunning(true);
    m_process->start();
}

void BenchmarkWindow::cancelProcess()
{
    if (!m_process)
        return;
    m_output->appendPlainText(tr("Остановка процесса..."));
    m_process->terminate();
    QTimer::singleShot(3000, m_process, [process = m_process]()
                       {
        if (process->state() != QProcess::NotRunning)
            process->kill(); });
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

bool BenchmarkWindow::eventFilter(QObject *obj, QEvent *event)
{
    // Перехватываем отпускание кнопки мыши на выпадающем списке
    if (obj == m_models->view()->viewport() && event->type() == QEvent::MouseButtonRelease)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        QListView *view = qobject_cast<QListView *>(m_models->view());
        QModelIndex index = view->indexAt(mouseEvent->pos());

        if (index.isValid())
        {
            QStandardItem *item = m_modelsModel->itemFromIndex(index);
            if (item && item->isCheckable())
            {
                Qt::CheckState currentState = item->checkState();
                item->setCheckState(currentState == Qt::Checked ? Qt::Unchecked : Qt::Checked);

                updateModelsDisplayText();

                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void BenchmarkWindow::updateModelsDisplayText()
{
    QStringList checkedModels;
    for (int i = 0; i < m_modelsModel->rowCount(); ++i)
    {
        if (m_modelsModel->item(i)->checkState() == Qt::Checked)
        {
            checkedModels.append(m_modelsModel->item(i)->text());
        }
    }

    if (checkedModels.isEmpty())
    {
        m_models->setEditText(tr("Выберите модели..."));
    }
    else if (checkedModels.size() == 1)
    {
        m_models->setEditText(checkedModels.first());
    }
    else
    {
        m_models->setEditText(checkedModels.join(","));
    }
}