# Сборка moe-swarm

## Требования и подготовка

Команды ниже предназначены для Ubuntu 24.04 и Ubuntu 24.04 в WSL2.
В WSL2 выполняйте их внутри Ubuntu; репозиторий лучше разместить в Linux
файловой системе, например `~/src/moe-swarm`.

Требуются CMake 3.22+, компилятор C++20, Ninja, Python 3 и Conan 2.
Установите инструменты и Conan в виртуальное окружение:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git ca-certificates \
  python3 python3-venv ccache clang-14 clang-format-14 clang-tidy-14
python3 -m venv .venv
source .venv/bin/activate
python -m pip install 'conan==2.34.0'
conan profile detect --force
```

В новом терминале снова активируйте окружение: `source .venv/bin/activate`.
Все последующие команды выполняются из корня репозитория.

## Сборка как в CI

Пресет `ci` собирает Release, включает тесты, `ccache` и
`SWARM_WERROR=ON`: предупреждения `-Wall -Wextra -Wpedantic` считаются ошибками.
llama.cpp отключена, поэтому submodule и модели для этой сборки не нужны.

```bash
conan install . -of=build/conan -s build_type=Release \
  -s compiler.cppstd=20 --build=missing
cmake --preset ci
cmake --build --preset ci --parallel 2
./build/bin/swarm --version
ctest --preset ci
ccache --show-stats
```

Исполняемый файл: `build/bin/swarm`. Тесты: `build/bin/tests/`.
`ctest --preset ci` выводит ошибки тестов и завершает работу с ошибкой,
если тестов нет.

## Debug и Release с llama.cpp

В пресетах `debug` и `release` сборка llama.cpp включена по умолчанию.
Сначала загрузите submodule:

```bash
git submodule update --init --recursive
```

Debug:

```bash
conan install . -of=build/conan -s build_type=Debug \
  -s compiler.cppstd=20 --build=missing
cmake --preset debug -DSWARM_BUILD_LLAMA=ON \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build --preset debug --parallel 2
ctest --preset debug
```

Release:

```bash
conan install . -of=build/conan -s build_type=Release \
  -s compiler.cppstd=20 --build=missing
cmake --preset release -DSWARM_BUILD_LLAMA=ON \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build --preset release --parallel 2
ctest --preset release
```

Пресеты используют общий каталог `build/`. При смене компилятора создайте
чистый каталог сборки и заново выполните `conan profile detect --force` с нужными
`CC` и `CXX`. При смене конфигурации заново выполните соответствующий
`conan install` и конфигурацию CMake.

| Опция CMake | По умолчанию | Назначение |
|---|---|---|
| `SWARM_WERROR` | `OFF` (в пресетах `ON`) | Предупреждения как ошибки |
| `SWARM_BUILD_TESTS` | `ON` | Сборка тестов |
| `SWARM_BUILD_LLAMA` | `ON` (в `ci` — `OFF`) | Сборка инструментов llama.cpp |

llama.cpp собирается через `ExternalProject` в `build/llama_cpp_build`.
Сохраняйте `build/` для повторных локальных сборок. Переданные выше параметры
launcher включают `ccache` также для C и C++ файлов llama.cpp; кэш сохраняется
и при пересоздании каталога сборки.

## Форматирование и статический анализ

CI использует clang-format 14 и clang-tidy 14. Стиль в `.clang-format`:
Google, отступ 4 пробела, ограничение строки 120 символов.

Проверка форматирования исходников и заголовков проекта:

```bash
git ls-files -z -- 'src/*.cpp' 'src/*.cc' 'src/*.cxx' \
  'src/*.h' 'src/*.hh' 'src/*.hpp' 'src/*.hxx' \
  'tests/*.cpp' 'tests/*.cc' 'tests/*.cxx' \
  'tests/*.h' 'tests/*.hh' 'tests/*.hpp' 'tests/*.hxx' \
  | xargs -0 -r clang-format-14 --dry-run --Werror --style=file
```

Исправление отдельного файла: `clang-format-14 -i src/main.cpp`.

Для clang-tidy нужна база `build/compile_commands.json`, которую создаёт
`cmake --preset ci`. Проверки берутся из `.clang-tidy`, все обнаруженные
предупреждения завершают проверку с ошибкой:

```bash
git ls-files -z -- 'src/*.cpp' 'src/*.cc' 'src/*.cxx' \
  'tests/*.cpp' 'tests/*.cc' 'tests/*.cxx' \
  | xargs -0 -r clang-tidy-14 -p build --warnings-as-errors='*'
```

## GitHub Actions

[Запуски CI](https://github.com/Yusko42/moe-swarm/actions/workflows/ci.yml),
[workflow](../.github/workflows/ci.yml).

CI запускается на каждый push в любую ветку, включая `dev` и `main`, и на
открытие, обновление или повторное открытие pull request. Также доступен ручной запуск.
Все задачи выполняются в контейнере `ubuntu:22.04`:

- `Build (gcc-11)` и `Build (clang-14)`: установка зависимостей через Conan,
  сборка по пресету `ci`, запуск `swarm --version` и `ctest --preset ci`.
- `Lint`: проверка clang-format и clang-tidy для файлов из `src/` и `tests/`.
- Артефакты `swarm-gcc-11` и `swarm-clang-14` содержат исполняемый файл `swarm`
  и хранятся 14 дней. После распаковки в Linux может потребоваться `chmod +x swarm`.

Кэши Conan и ccache разделены по компиляторам. Кэш ccache переиспользуется
между коммитами; статистика видна в шаге `Show compiler cache statistics`.
llama.cpp в CI не загружается и не собирается. Модели и пользовательские
секреты не используются; workflow имеет только разрешение `contents: read`.

Проверки с намеренными ошибками выполняются в отдельной временной ветке:
ошибка компиляции или предупреждение должны уронить сборку, нарушение
форматирования — задачу `Lint`. После push успешный запуск можно открыть
по ссылке выше и скопировать его постоянную ссылку из GitHub Actions.
