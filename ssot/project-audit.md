# Аудит проекта

Дата аудита: 2026-06-04.

## Короткая суть

Репозиторий содержит учебный C++ desktop-прототип для практик по Android CellInfo, ZMQ, PostgreSQL, ImGui/ImPlot, OpenStreetMap и heatmap/IDW. Важная для дальнейшей работы часть - приложение `server`, которое принимает данные от Android-клиента и показывает их в окне "Cell Monitor".

Мобильного Android-проекта в этом репозитории не видно. В репозитории есть только сервер/просмотровщик на C++ и его vendored UI-зависимости.

## Основные директории

- `src/` - C++ реализация приложения.
- `include/` - заголовки, DTO и конфигурация.
- `third_party/imgui`, `third_party/implot`, `third_party/stb` - внешние библиотеки, положенные в репозиторий.
- `ssot/` - Single Source of Truth для документации проекта.
- `build/`, `build_wsl/` - уже сгенерированные сборки, CMake-файлы, бинарники, логи, кэши тайлов и heatmap. Это не исходный код.
- `.obsidian/` - vault-настройки Obsidian. Командные плагины/темы можно хранить, локальное состояние рабочего пространства игнорируется.

## Сборка

`CMakeLists.txt` описывает проект `server_compact`, C++17, target `server`.

Зависимости:

- SDL2
- OpenGL
- GLEW
- ZeroMQ/libzmq через pkg-config
- PostgreSQL/libpq
- CURL
- nlohmann_json
- pthread
- vendored ImGui/ImPlot/stb

Проверочная сборка в текущем окружении не запускалась, потому что команда `cmake` отсутствует. По статическому чтению есть явные ошибки компиляции в `src/GuiManager.cpp` и `src/MapManager.cpp`.

## Поток выполнения

1. `src/main.cpp` инициализирует SDL2, OpenGL/GLEW, ImGui и ImPlot.
2. Создаются `ServerCore`, `MapManager`, `GuiManager`.
3. `MapManager::initGL()` сейчас пустой, текстуры создаются лениво.
4. `ServerCore::start()` подключается к PostgreSQL, создает ZMQ REP-сокет `tcp://*:5000`, запускает поток приема.
5. До старта GUI загружаются агрегированные точки из БД через `loadAggregatedPoints()` и передаются в `GuiManager`.
6. Главный цикл SDL/ImGui каждый кадр вызывает `gui.render()`, проверяет новые данные и при первой валидной точке пытается автоцентрировать карту.
7. При выходе вызываются `server.stop()`, shutdown ImGui/ImPlot/SDL.

## Модель данных

`include/DataModels.h` задает DTO:

- `LocationDto`: latitude, longitude, altitude, timestamp, speed, accuracy.
- `LteCellDto`: band, earfcn, mcc, mnc, pci, tac, rsrp, rsrq, rssnr, cqi, timingAdvance.
- `GsmCellDto`, `NrCellDto`, `TrafficDto` объявлены, но фактически почти не используются.
- `Measurement` содержит location, LTE/GSM/NR cells, traffic, deviceId, receivedTime.
- `MapPoint` содержит lat/lon/value/pci/isCurrent. Для полноценного выбора RSRP/RSRQ/RSSI/Altitude и EARFCN модели недостаточно.
- `AggregatedPoint` содержит только lat/lon/rsrp/count.

## ServerCore

Файл: `src/ServerCore.cpp`.

Отвечает за:

- создание ZMQ REP-сервера;
- разбор входящего JSON в `Measurement`;
- хранение истории `m_history` и последнего измерения;
- запись в PostgreSQL;
- резервную запись в `measurements.json`;
- загрузку агрегированных точек из PostgreSQL.

Ограничения текущей реализации:

- `parseJson()` разбирает location и `lteCells`, но не заполняет GSM, NR и traffic, хотя структуры объявлены.
- В БД сохраняется только первая LTE-сота.
- Таблица `measurements` не хранит все поля из задания: нет EARFCN-разделения для heatmap по каждому EARFCN, нет RSSI, нет GSM/NR/traffic.
- Настройки БД и пароль захардкожены в `include/Config.h`.

## GuiManager

Файл: `src/GuiManager.cpp`.

Окна:

- `Current Data` - deviceId, координаты, высота, accuracy, первая LTE-сота.
- `Signal Plots` - график RSRP по истории.
- `Statistics` - количество измерений в памяти.
- `Map Controls` - zoom, переключатель heatmap, критерий, радиус.
- `OSM Map` - карта, панорамирование, zoom колесом, текущая и агрегированные точки.

Текущее состояние проблемное: блок работы с агрегированными точками содержит повторное объявление `aggregatedPoints`, использует `aggVec` вне области видимости и выглядит как незавершенный merge/черновик.

## MapManager

Файл: `src/MapManager.cpp`.

Отвечает за:

- перевод координат lon/lat в OSM tile x/y и обратно;
- загрузку PNG-тайлов через curl;
- disk cache в `tile_cache/<zoom>/<x>/<y>.png`;
- хранение тайлов в памяти;
- создание OpenGL-текстур;
- генерацию heatmap-текстуры методом IDW;
- отрисовку карты, heatmap и точек через ImGui draw list.

Важная особенность: расчет теплокарты сейчас синхронный и вызывается прямо из `renderMap()` при каждом кадре, когда включен `showHeatmap`.

## Репозиторий и мусор

В git уже отслеживаются `build/` и `build_wsl/`, включая CMake cache, object files, static libraries, binary, `measurements.json`, `imgui.ini` и сотни PNG OSM-тайлов. Это раздувает репозиторий и мешает видеть реальные изменения.

Добавлен `.gitignore`, чтобы новые артефакты не попадали в git. Но уже отслеживаемые файлы останутся в индексе, пока их отдельно не убрать через `git rm --cached` в cleanup-коммите.
