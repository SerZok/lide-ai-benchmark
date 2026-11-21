# lide-ai-benchmark

Standalone Qt application for Common Lisp code-generation evaluations. The CLI
queries an Ollama-compatible `/api/chat` endpoint, extracts Lisp source, runs
task tests in the Windows AppContainer SBCL sandbox, and flushes one JSON object
per model/task/repetition to JSON Lines. Its `--gui` mode provides a desktop
front end for configuring and running the CLI and aggregating JSONL reports.

## Build

Requires CMake 3.19+, C++17, Qt 6 Core, Network and Widgets, Python 3 for
aggregation, and Windows for the isolated SBCL runner.

```powershell
cmake -S benchmark -B benchmark/build -DCMAKE_PREFIX_PATH="C:\Qt\6.10.2\msvc2022_64"
cmake --build benchmark/build --config Release
```

The root LIDE CMake project also builds `lide-ai-benchmark` as a separate
executable beside LIDE. Its **Опции → Исследовательский бенчмарк моделей...**
menu item launches the benchmark GUI and passes the configured endpoint, model,
SBCL path, and API key from LIDE's settings.

## Configure and run

Copy `benchmark.example.json` and edit the model, task prompts, Lisp function
names, expected expressions and SBCL executable path. Test `arguments` and
`expected` fields are Common Lisp expressions encoded as JSON strings. Paths in
the configuration are relative to the configuration file.

```powershell
benchmark\build\Release\lide-ai-benchmark.exe --config benchmark\my-suite.json
benchmark\build\Release\lide-ai-benchmark.exe --gui
python benchmark\aggregator.py benchmark\results.jsonl
```

The runner accepts `--output`, `--sbcl`, and `--endpoint` overrides. Set
`OLLAMA_API_KEY` in the environment when the Ollama-compatible endpoint needs
Bearer authentication. Output is truncated when a run starts, then flushed
after every result so an interrupted run retains completed records. A model's
exact Ollama digest and quantization are recorded when available; missing
metadata is marked `unknown` or reported in `modelInfoError`.

The JSONL records include generation latency, TTFT, token counts, token rate,
server/model metadata, raw response, extracted source, sandbox results, and
error category. `pass@1`, CER and RVR use only each task's first run;
error-category distributions include all repetitions. CER counts SBCL read /
load errors; RVR counts timeouts, memory-limit termination and output-limit
termination. Infrastructure and generation failures are kept as separate
categories and are not silently included in CER or RVR.

## Task format

```json
{
  "id": "count-occurrences",
  "prompt": "Implement COUNT-OCCURRENCES. Return Common Lisp code.",
  "function": "COUNT-OCCURRENCES",
  "tests": [
    { "name": "duplicates", "arguments": "2 '(2 3 2)", "expected": "2" }
  ]
}
```

---

# lide-ai-benchmark

Стандартизированное Qt-приложение для оценки генерации кода на Common Lisp.
CLI-интерфейс обращается к Ollama-совместимому эндпоинту `/api/chat`, извлекает
Lisp-код, запускает тесты задач в изолированном SBCL-песочнице (Windows
AppContainer) и записывает результаты в JSON Lines (JSONL) в виде одного JSON-объекта
на каждую модель/задачу/повтор. Режим `--gui` предоставляет графический интерфейс
для настройки и запуска бенчмарка, а также для агрегации JSONL-отчётов.

## Сборка

Требуется CMake 3.19+, C++17, Qt 6 (модули Core, Network, Widgets), Python 3
для агрегации и Windows для изолированного SBCL-раннера.

```powershell
cmake -S benchmark -B benchmark/build -DCMAKE_PREFIX_PATH="C:\Qt\6.10.2\msvc2022_64"
cmake --build benchmark/build --config Release
```

Корневой CMake-проект LIDE также собирает `lide-ai-benchmark` как отдельный
исполняемый файл рядом с LIDE. Пункт меню **Опции → Исследовательский бенчмарк
моделей...** запускает GUI бенчмарка и передаёт настроенные эндпоинт, модель,
путь к SBCL и API-ключ из настроек LIDE.

## Настройка и запуск

Скопируйте `benchmark.example.json` и отредактируйте: модель, промпты задач,
имена Lisp-функций, ожидаемые выражения и путь к исполняемому файлу SBCL.
Поля `arguments` и `expected` в тестах — это выражения Common Lisp, закодированные
как JSON-строки. Пути в конфигурации указываются относительно файла конфигурации.

```powershell
benchmark\build\Release\lide-ai-benchmark.exe --config benchmark\my-suite.json
benchmark\build\Release\lide-ai-benchmark.exe --gui
python benchmark\aggregator.py benchmark\results.jsonl
```

Утиита поддерживает переопределение параметров через `--output`, `--sbcl` и
`--endpoint`. Установите переменную окружения `OLLAMA_API_KEY`, если Ollama-
совместимый эндпоинт требует Bearer-аутентификации. Вывод очищается при начале
запуска и сбрасывается на диск после каждого результата, поэтому прерванный
запуск сохраняет завершённые записи. Точный Ollama-digest и квантование модели
записываются, если доступны; отсутствующая метка помечается как `unknown` или
сообщается в `modelInfoError`.

JSONL-записи включают задержку генерации, TTFT (время до первого токена),
количество токенов, скорость генерации, метаданные сервера/модели, исходный
ответ, извлечённый код, результаты песочницы и категорию ошибки. Метрики
`pass@1`, CER и RVR рассчитываются только по первой попытке для каждой задачи;
распределение категорий ошибок включает все повторы. CER считает ошибки чтения/загрузки
SBCL, RVR — таймауты, превышение лимита памяти и лимита вывода. Ошибки инфраструктуры
и генерации хранятся в отдельных категориях и не входят в CER и RVR.

## Метрики CER и RVR

`pass@1`, CER и RVR рассчитываются только по первой попытке (attempt 1) для каждой
пары модель-задача.

- **CER** (Compilation/Read Error Rate) — доля задач с ошибками компиляции или чтения
  (SBCL не смог загрузить сгенерированный код).
- **RVR** (Resource Violation Rate) — доля задач, завершившихся из-за исчерпания
  ресурсов: таймаут, превышение лимита памяти или объёма вывода.

Ошибки генерации и извлечения кода (generation_error, extraction_error) не входят
в CER и RVR, но учитываются в общем знаменателе.

## Формат задачи

```json
{
  "id": "count-occurrences",
  "prompt": "Implement COUNT-OCCURRENCES. Return Common Lisp code.",
  "function": "COUNT-OCCURRENCES",
  "tests": [
    { "name": "duplicates", "arguments": "2 '(2 3 2)", "expected": "2" }
  ]
}
```
