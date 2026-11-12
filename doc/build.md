# Сборка moe-swarm

---

## 1. Требования

### Системы сборки

| Инструмент | Минимальная версия |
|-------|------|
| CMake | 3.22 |
| Conan | 2.x  |
|Компилятор| Поддержка c++20|

---

## 2. Подготовка

```bash

git submodule update --init --recursive
```

Установка профиля Conan:

```bash
conan profile detect --force
```

---

## 3. Сборка

#### 3.1 Debug

```bash
conan install . -of=build/conan -s build_type=Debug --build=missing
cmake . --preset=debug   
cmake --build build
```

#### 3.2 Release

```bash
conan install . -of=build/conan -s build_type=Release --build=missing
cmake . --preset=release  
cmake --build build
```

### 3.3 Результаты сборки

**build/bin/swarm(.exe)**

### 3.4 Опции CMake

| Опция | Назначение |
|---|---|
| `SWARM_WERROR` | Предупреждения как ошибки |
| `SWARM_BUILD_TESTS` | Включить сборку тестов |

---

## 4. Тесты

```bash
cd build
ctest --output-on-failure
```

---

## 5. clang-format

Конфигурация: `.clang-format` в корне репозитория.

```bash
clang-format -i <filename>
```

---

## 6. clang-tidy

Конфигурация: `.clang-tidy` в корне. Использует
`compile_commands.json` из корня проекта.

```bash
clang-tidy -p . <filename>
```

---

### Долгая сборка llama.cpp

llama.cpp собирается через `ExternalProject` при каждом чистом
`build/`. Чтобы переиспользовать, оставьте каталог
`build/llama.cpp-build` между конфигурациями — CMake не будет
пересобирать его, пока не изменится коммит submodule.