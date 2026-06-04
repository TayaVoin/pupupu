# Stage Environment

Дата актуализации: 2026-06-04.

## Назначение

`stage environment` - Windows-компьютер студентки с WSL, находящийся в одной сети с Android-телефоном. Здесь выполняются реальная сборка, запуск и дебаг приложения.

Именно stage нужен для проверки:

- приема Android-данных по ZMQ;
- доступности порта сервера в локальной сети;
- записи в PostgreSQL;
- поведения ImGui/SDL/OpenGL окна;
- отображения OpenStreetMap-тайлов;
- текущей точки, графиков и heatmap.

## Известная конфигурация

Точная версия Windows/WSL/Ubuntu пока не зафиксирована. По старой папке `build_wsl/` видно, что проект уже собирался в WSL. `build_wsl/` является локальной сборочной папкой stage и не должна попадать в git.

## Установка зависимостей в WSL

Ожидаемый набор пакетов для Ubuntu/WSL:

```bash
sudo apt update
sudo apt install cmake g++ pkg-config libsdl2-dev libglew-dev libgl1-mesa-dev libzmq3-dev cppzmq-dev libpq-dev libcurl4-openssl-dev nlohmann-json3-dev
```

Зачем нужны ключевые пакеты:

- `libsdl2-dev` - окно, события и интеграция ImGui SDL2 backend.
- `libglew-dev` и `libgl1-mesa-dev` - OpenGL/GLEW.
- `libzmq3-dev` - ZeroMQ C-библиотека `libzmq`.
- `cppzmq-dev` - C++ header `zmq.hpp`.
- `libpq-dev` - PostgreSQL client library.
- `libcurl4-openssl-dev` - загрузка OSM-тайлов через curl.
- `nlohmann-json3-dev` - JSON parser.
- `pkg-config` - поиск libzmq через CMake `pkg_check_modules`.

## Сборка на stage

Допустимо использовать локальную ignored-папку `build_wsl/`:

```bash
cmake -S . -B build_wsl
cmake --build build_wsl --target server
./build_wsl/server
```

Можно использовать и обычную `build/`, если так удобнее:

```bash
cmake -S . -B build
cmake --build build --target server
./build/server
```

Обе папки игнорируются git и не должны коммититься.

## Runtime-проверка

Перед запуском проверить `include/Config.h`:

- `ZMQ_HOST = "*"`
- `ZMQ_PORT = 5000`
- PostgreSQL host/port/db/user/password
- пути `TILE_CACHE_DIR`, `JSON_LOG_FILE`

Android-телефон должен отправлять данные на IP stage-компьютера и порт `5000`. Stage-компьютер и телефон должны быть в одной сети, либо должна быть настроена маршрутизация/проброс портов.

## PostgreSQL

Проект ожидает PostgreSQL с параметрами из `include/Config.h`. На 2026-06-04 там захардкожены:

```text
DB_HOST = localhost
DB_PORT = 5432
DB_NAME = cell_data
DB_USER = visual
DB_PASS = visualprog
```

Это технический долг: в будущем лучше вынести настройки БД в env/config, чтобы не хранить пароль в исходниках.

## Линковка и CMake-особенности

- CMake minimum: 3.14.
- ZeroMQ ищется через `pkg-config` и линкуется imported target `PkgConfig::ZMQ`.
- GLEW линкуется imported target `GLEW::GLEW`.
- `stdc++fs` добавляется только для GNU C++ compiler версии меньше 9. На современных GCC в WSL он не нужен.
- Homebrew-specific paths в `CMakeLists.txt` применяются только при `APPLE`, поэтому не должны влиять на WSL.

## Ожидаемая совместимость CMake

Текущий `CMakeLists.txt` должен работать в обоих окружениях:

- dev/macOS: подтверждено CMake configure + build до `[100%] Built target server`;
- stage/WSL: ожидается совместимость через apt-пакеты и `pkg-config`, но фактическую сборку и runtime нужно подтвердить на stage.

Если stage-сборка не найдет GLEW или ZeroMQ, сначала проверить установку `libglew-dev`, `libzmq3-dev`, `cppzmq-dev`, `pkg-config`, затем вывод `pkg-config --libs libzmq`.

## Что нельзя делать на stage

- Не коммитить `build/`, `build_wsl/`, `tile_cache/`, `measurements.json`, `heatmap_cache/`, `imgui.ini`.
- Не возвращать старые build-артефакты в git.
- Не считать успешную dev-сборку заменой stage runtime-проверки.
