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