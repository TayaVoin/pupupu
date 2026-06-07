# Cell Monitor Server

Учебный C++ desktop-проект для приема измерений мобильной сети от
Android-приложения. Сервер принимает JSON по ZMQ, хранит данные в PostgreSQL и
резервном JSON-файле, показывает LTE-параметры и графики в ImGui/ImPlot, а также
рисует OpenStreetMap-карту с точками и IDW-теплокартой.

Android-проект в этом репозитории отсутствует. Здесь находится серверная и
desktop GUI часть.

## Возможности

- ZMQ REP-сервер на `tcp://*:5000`;
- разбор location и LTE CellInfo из JSON;
- ответ `OK` после обработки сообщения;
- хранение истории измерений в памяти;
- запись в PostgreSQL и `measurements.json`;
- отображение текущих координат и LTE-параметров;
- график RSRP через ImPlot;
- динамическая загрузка нескольких OSM-тайлов;
- дисковый кэш тайлов `tile_cache/zoom/x/y.png`;
- pan и zoom карты;
- выбор RSRP, RSRQ, RSSI или Altitude для heatmap;
- фильтрация heatmap по EARFCN;
- радиус IDW от 10 до 40 метров;
- расчет IDW в отдельном worker thread;
- сохранение рассчитанного PNG в `heatmap_cache/`.

## Архитектура

- `src/main.cpp` инициализирует SDL2, OpenGL/GLEW, ImGui/ImPlot и главный цикл.
- `ServerCore` отвечает за ZMQ, JSON, историю и PostgreSQL.
- `GuiManager` строит окна текущих данных, графиков, статистики и карты.
- `MapManager` загружает OSM-тайлы, управляет кэшем и формирует heatmap.
- `include/DataModels.h` содержит DTO измерений и точек карты.

## Репозиторий

- `src/` и `include/` - исходный код;
- `third_party/` - vendored ImGui, ImPlot и stb;
- `ssot/` - актуальная документация, аудиты и отчет;
- `build/`, `build_wsl/`, `tile_cache/`, `heatmap_cache/` - локальные
  генерируемые данные, которые не должны попадать в git.

## Сборка

Актуальные зависимости и команды:

- macOS/dev: `ssot/env/dev.md`;
- Windows/WSL stage: `ssot/env/stage.md`;
- порядок разработки и проверки: `ssot/development-process.md`.

Проверенная dev-команда:

```bash
cmake -S . -B /private/tmp/pupupu-cmake-audit \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /private/tmp/pupupu-cmake-audit \
  --target server --clean-first --parallel 4
```

На 2026-06-07 результат:

```text
[100%] Built target server
```

Полноценный запуск с Android, PostgreSQL, GUI и сетью OSM выполняется на stage
environment.

## Текущее состояние

Проект собирается и содержит реализацию основных подсистем задания. Несколько
частей пока выполнены частично:

- в БД и графиках используется только первая LTE-сота;
- схема PostgreSQL не содержит колонку RSSI, которую запрашивает загрузчик;
- график не разделен по PCI и показывает только RSRP;
- GSM, NR и traffic не разбираются;
- позиционирование карты требует единой Web Mercator геометрии;
- invalidation и сохранение heatmap требуют доработки;
- runtime на stage еще нужно подтвердить.

## Документация

- `ssot/user story.md` - исходные задания;
- `ssot/project-audit.md` - полный аудит кода и соответствия заданию;
- `ssot/performance-and-problems-audit.md` - аудит производительности и
  надежности;
- `ssot/implementation-report.md` - отчет о реализованной функциональности;
- `ssot/remediation-plan.md` - приоритетный план исправлений с примерами и
  критериями готовности.
