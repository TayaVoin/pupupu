# Dev Environment

Дата актуализации: 2026-06-04.

## Назначение

`dev environment` - MacBook с Codex. Здесь выполняются ИИ-кодинг, чтение проекта, правки исходников, ведение SSOT-документации и проверочные CMake/build-запуски.

Этот контур не предназначен для полноценного runtime-дебага приложения: Android-телефон не подключается сюда как основной источник данных, ZMQ-прием, PostgreSQL и GUI-карта проверяются на `stage environment`.

## Известная конфигурация

- OS: macOS на MacBook.
- Compiler: AppleClang 21.0.0.21000101.
- CMake: 4.3.3.
- Homebrew prefix: `/opt/homebrew`.
- Рабочий каталог проекта: `/Users/dev/dev/Taya/pupupu`.
- Локальные `build/` и `build_wsl/` удалены и игнорируются.

## Установка зависимостей

На dev были установлены зависимости через Homebrew:

```bash
brew install sdl2 glew zeromq cppzmq postgresql libpq curl nlohmann-json pkg-config
```

Проверенные версии:

```text
curl 8.20.0
glew 2.3.1
libpq 18.4
nlohmann-json 3.12.0
pkgconf 2.5.1
postgresql@18 18.4
sdl2 2.32.10
zeromq 4.3.5_2
cppzmq 4.11.0
```

`cppzmq` нужен отдельно от `zeromq`: код включает `#include <zmq.hpp>`, а этот C++ header wrapper поставляется пакетом `cppzmq`, не самим `zeromq`.

## Проверка сборки

Проверочные сборки на dev выполняются только во временной директории вне репозитория:

```bash
cmake -S . -B /private/tmp/pupupu-cmake-check
cmake --build /private/tmp/pupupu-cmake-check --target server
rm -rf /private/tmp/pupupu-cmake-check
```

После правок в `CMakeLists.txt` отдельный `-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/libpq` не нужен: проект сам добавляет Homebrew-пути `/opt/homebrew/opt/libpq` и `/usr/local/opt/libpq` на macOS.

Последний проверенный результат:

```text
[100%] Built target server
```

Проверено командой без дополнительных CMake-флагов.

## Линковка и CMake-особенности

- GLEW подключается через imported target `GLEW::GLEW`.
- ZeroMQ подключается через `pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq)` и линкуется как `PkgConfig::ZMQ`.
- PostgreSQL ищется через `find_package(PostgreSQL REQUIRED)`. На Homebrew `libpq` keg-only, поэтому `CMakeLists.txt` добавляет `/opt/homebrew/opt/libpq` и `/usr/local/opt/libpq` в `CMAKE_PREFIX_PATH` только на Apple.
- `cppzmq` является header-only wrapper для `zmq.hpp`; отдельная линковка для него не нужна.
- `stdc++fs` не линкуется на macOS. Он добавляется только для GNU C++ compiler версии меньше 9.
- CMake на dev сейчас находит CURL из macOS SDK (`libcurl.tbd`), хотя Homebrew curl тоже установлен. Это нормально для compile-check; если понадобятся конкретные возможности Homebrew curl, нужно будет отдельно направить CMake на `/opt/homebrew/opt/curl`.

## Известные warnings

Сборка проходит, но есть не блокирующие предупреждения:

- `third_party/stb/stb_image_write.h` использует deprecated `sprintf` на macOS.
- Линкер пишет `ignoring duplicate libraries: 'libimgui.a'`.

Эти warnings не мешают dev-сборке и не являются текущими блокерами.

## Запрещено на dev

- Не запускать GUI-приложение как полноценный runtime-сценарий.
- Не создавать постоянные `build/` или `build_wsl/` в репозитории.
- Не коммитить runtime-файлы: `measurements.json`, `tile_cache/`, `heatmap_cache/`, `imgui.ini`.
