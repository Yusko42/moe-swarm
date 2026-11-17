# Проверка реализации, 2026-11-17

Ветка `feature/gguf-memory-estimator` создана от локального `dev`
(`b181206c9c741becf9643f0e15f33f0f718a17b6`).

## Локальная среда

Windows, MinGW GCC 14.2.0, CMake 3.31.0-rc3, Python 3.10.11,
NumPy 2.2.6. Для локальной сборки в игнорируемом `build-profile/`
установлены CLI11 2.6.1 и GoogleTest 1.17.0. Зависимости Conan проекта
(CLI11 2.7.2, GoogleTest 1.18.0) не менялись. Первичная Windows-проверка
использовала более старые зависимости; отдельная Linux-проверка описана ниже.

```powershell
cmake -S . -B build-profile/project -G "MinGW Makefiles" `
  -DSWARM_BUILD_LLAMA=OFF -DSWARM_WERROR=ON `
  -DCMAKE_PREFIX_PATH="$PWD/build-profile/deps" -DCMAKE_BUILD_TYPE=Release
cmake --build build-profile/project -j 4
ctest --test-dir build-profile/project --output-on-failure
```

Сборка с `-Wall -Wextra -Wpedantic -Werror` прошла. CTest: **6/6**,
включая исходный dummy-test, три C++ теста профиля, Python-проверку reader/CLI
и Python-проверку сравнения логов. Python-проверки используют синтетические
файлы и логи; реальная модель и стенд не использовались.

`clang-format` 19.1.3 и `clang-tidy` 19.1.3 с конфигурацией проекта
прошли для всех четырёх добавленных/изменённых C++ translation units
и заголовков. Для clang-tidy указан target MinGW и системные include-пути
этого компилятора. Это первичный прогон; проверка версий 14 приведена ниже.
`git diff --check` прошёл.

## Проверка инструментами CI в WSL

Проверены workflow `.github/workflows/ci.yml`, пресет `ci` и конфигурации
линтеров. Для ветки `feature/gguf-memory-estimator` запусков GitHub Actions
на момент проверки ещё нет: изменения остаются в рабочем дереве.

Среда: Ubuntu 24.04 в WSL, GCC 11.5.0, Clang/clang-format/clang-tidy 14.0.6,
Conan 2.34.0, Python 3.12.3. Использованы зависимости из `conanfile.txt`:
**CLI11 2.7.2 и GoogleTest 1.18.0**. У Clang явно выбраны заголовки
`libstdc++ 11`, соответствующие Ubuntu 22.04 в CI.

Отличия среды от GitHub runner: Ubuntu 24.04 вместо 22.04 и системный
**CMake 3.28.3 вместо 3.31.6**. Системный CMake объявлен в локальном
Conan-профиле как `[platform_tool_requires] cmake/3.28.3`, чтобы не скачивать
дополнительный CMake. Workflow, зависимости и настройки проверок проекта
не ослаблялись.

- `clang-format-14 --dry-run --Werror --style=file`: прошёл для всех
  исходников и заголовков, включая новые файлы.
- `clang-tidy-14 --warnings-as-errors='*'`: полный проход по всем C++
  исходникам с конфигурацией проекта прошёл. Затем повторён по базе
  компиляции Conan-сборки: `clang-tidy-14 ... -p build-profile/ci-clang14
  --warnings-as-errors='*'`; результат также успешный, код возврата 0.
- Release с GCC 11, пресет `ci`, `SWARM_WERROR=ON`, `SWARM_BUILD_LLAMA=OFF`:
  сборка и `swarm --version` прошли; CTest **6/6**.
- Release с Clang 14, те же опции: сборка и `swarm --version` прошли;
  CTest **6/6**.

## Эталон GGUFReader

Использован неизменённый пакет `gguf` из архива llama.cpp
`cd959bae99ff4bed22bef2604f45241c09bbb8c8` через `PYTHONPATH`:

```powershell
$env:PYTHONPATH="$PWD/build-profile/llama.cpp-cd959bae99ff4bed22bef2604f45241c09bbb8c8/gguf-py"
python tests/gguf_oracle_test.py build-profile/project/bin/swarm.exe
```

Все **36** типов из `GGML_QUANT_SIZES` совпали по форме, ID, размеру
и абсолютному смещению. Сумма тестовых тензоров — **6303 байта**, начало
данных — **1856**. Повторено для v2 little-endian, v3 little-endian,
v3 big-endian. Также совпали базовый пример (512 байт тензоров,
64 байта padding), пример с метаданными и синтетическая Qwen3 (50 тензоров).

Qwen3-проверки: 48 слоёв, **16 MiB KV на слой**, **768 MiB всего**,
**384 MiB** на узел при раскладке 24+24. Проверены точная граница лимита,
дублирование внеблочных весов между узлами и ошибки раскладки.

## Стенд #19160

Логов пока нет — подтверждено пользователем. Реальные расхождения
**не измерены**. Для будущей сверки добавлены `tools/compare_buffers.py`,
тесты его расчётов и [инструкция](model-profile.md#сравнение-с-19160).
