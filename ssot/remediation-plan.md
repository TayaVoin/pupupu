# План исправления замечаний аудита

Дата: 2026-06-07.

## Правила выполнения

Приоритеты:

- `P0`: потеря данных, нарушение обязательного задания или неверный результат;
- `P1`: существенная runtime-проблема, race, зависание или дорогой hot path;
- `P2`: надежность, масштабирование и эксплуатация;
- `P3`: техническая чистка без изменения основной функциональности.

Задачи следует выполнять сверху вниз. После каждого P0/P1 изменения нужна
чистая сборка на dev. Проверка GUI, PostgreSQL, ZMQ и сети выполняется на stage.

## P0. Целостность PostgreSQL и RSSI

### Цель

Согласовать создание таблицы, INSERT и SELECT так, чтобы новая и существующая
БД поддерживали RSSI.

### Где менять

- `src/ServerCore.cpp`: `createTable()`, `saveToDatabase()`,
  `loadAggregatedPoints()`;
- при выделении миграций: новый простой SQL-файл или функция migration рядом с
  DB initialization;
- `ssot/env/stage.md`: команда проверки схемы.

### Реализация

1. Добавить `lte_rssi INT` в `CREATE TABLE`.
2. После создания таблицы выполнить идемпотентную миграцию для старой БД.
3. Добавить RSSI в buffers/parameters и список колонок INSERT.
4. Создать индекс по `timestamp DESC`.
5. Перед `atoi/atof` проверять `PQgetisnull()`.
6. При ошибке миграции не считать БД готовой.

Пример:

```sql
ALTER TABLE measurements
    ADD COLUMN IF NOT EXISTS lte_rssi INT;

CREATE INDEX IF NOT EXISTS measurements_timestamp_idx
    ON measurements(timestamp DESC);
```

Лучше передавать числовые значения через `PQexecParams`, а SQL NULL задавать
нулевым указателем параметра вместо sentinel-значений.

### Проверка

- новая пустая БД создается без ручных ALTER;
- старая БД обновляется повторным запуском;
- INSERT с RSSI `-87` возвращает `PGRES_COMMAND_OK`;
- SELECT возвращает RSRP, RSRQ, RSSI, Altitude и EARFCN;
- отсутствие RSSI не приводит к чтению null pointer.

### Готово, когда

`loadAggregatedPoints()` работает на схеме, которую создает само приложение, и
RSSI доходит от JSON до `AggregatedPoint`.

## P0. Несколько LTE-сот и графики по PCI

### Цель

Не терять `lteCells[1..N]` и выполнить требование отображать несколько PCI
параллельно.

### Где менять

- `include/DataModels.h`: модели DB/plot при необходимости;
- `src/ServerCore.cpp`: сохранение и подготовка plot data;
- `include/ServerCore.h`: snapshot API для серий;
- `src/GuiManager.cpp`: current data, plots и map points.

### Схема данных

Разделить общую запись местоположения и записи сот:

```sql
CREATE TABLE measurements (
    id BIGSERIAL PRIMARY KEY,
    timestamp BIGINT,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    altitude DOUBLE PRECISION,
    accuracy REAL,
    speed REAL,
    device_id TEXT,
    received_time BIGINT
);

CREATE TABLE cell_measurements (
    id BIGSERIAL PRIMARY KEY,
    measurement_id BIGINT NOT NULL
        REFERENCES measurements(id) ON DELETE CASCADE,
    radio_type TEXT NOT NULL DEFAULT 'LTE',
    pci INT,
    earfcn INT,
    rsrp INT,
    rsrq INT,
    rssi INT,
    rssnr INT,
    band INT,
    tac INT
);
```

### Реализация

1. Начать DB transaction.
2. Вставить measurement через `RETURNING id`.
3. Для каждого элемента `m.lteCells` вставить строку `cell_measurements`.
4. Commit только после успешной записи всех сот, иначе rollback.
5. Сформировать series key как минимум из PCI; если PCI может повторяться на
   разных EARFCN, использовать пару `(earfcn, pci)`.
6. Хранить ограниченные серии RSRP, RSSI и RSSNR.
7. В ImPlot вызывать `PlotLine` для каждой серии с label вроде
   `PCI 13 / EARFCN 1650`.
8. Создавать `MapPoint` для каждой LTE-соты текущего measurement.

Пример модели:

```cpp
struct SignalSeries {
    int pci = 0;
    int earfcn = 0;
    std::vector<float> rsrp;
    std::vector<float> rssi;
    std::vector<float> rssnr;
};
```

### Проверка

Контрольный JSON содержит две LTE-соты:

```json
{
  "location": {"latitude": 55.0, "longitude": 83.0},
  "lteCells": [
    {"pci": 7, "earfcn": 1650, "rsrp": -100, "rssi": -75},
    {"pci": 13, "earfcn": 1650, "rsrp": -89, "rssi": -66}
  ]
}
```

Ожидается одна measurement row, две cell rows, две линии в легенде и две
`MapPoint`.

### Готово, когда

Ни один LTE-элемент входного массива не теряется, а GUI показывает отдельные
серии всех PCI.

## P0. Корректность IDW

### Цель

Получать прозрачную heatmap вне радиуса данных и надежно пересчитывать результат
при любом изменении входа.

### Где менять

- `include/MapManager.h`: request/result structures и generation id;
- `src/MapManager.cpp`: `requestHeatmapUpdate()`, worker,
  `computeHeatmapPixels()`;
- `src/GuiManager.cpp`: передача RSSI, `idwPower`, EARFCN.

### Реализация

1. Заменить live `p.rssi = lte.rsrp` на `p.rssi = lte.rssi`.
2. Передать `idwPower` в request и compute function.
3. Если `sumW == 0`, оставить пиксель прозрачным.
4. Использовать `std::pow(distance, idwPower)`.
5. Ввести монотонный `requestGeneration`.
6. Snapshot должен содержать центр, zoom, размер, radius, power, criterion,
   EARFCN и points.
7. Worker публикует результат только если его generation все еще актуален.
8. Data version увеличивать при любом новом measurement/DB snapshot. Это
   надежнее дорогого hash всех точек.

Пример:

```cpp
if (sumW == 0.0) {
    pixels[idx + 0] = 0;
    pixels[idx + 1] = 0;
    pixels[idx + 2] = 0;
    pixels[idx + 3] = 0;
    continue;
}

const double weight = 1.0 / std::pow(distance, idwPower);
```

Пример request:

```cpp
struct HeatmapRequest {
    uint64_t generation;
    uint64_t dataVersion;
    int zoom;
    int width;
    int height;
    int criterion;
    int earfcn;
    float radiusMeters;
    float power;
    double centerLat;
    double centerLon;
    std::vector<MapPoint> points;
};
```

### Проверка

- один sample окрашивает только область в заданном радиусе;
- RSRP, RSRQ, RSSI и Altitude дают разные ожидаемые цвета;
- смена EARFCN меняет набор точек;
- изменение не первой точки инициирует расчет;
- быстрые pan/zoom не публикуют устаревший результат;
- `idwPower=1` и `idwPower=2` дают разные контрольные значения.

### Готово, когда

Пустые зоны прозрачны, используются реальные значения критерия, а результат
однозначно соответствует последнему запросу.

## P0. Единая Web Mercator геометрия карты

### Цель

Точно располагать центр, тайлы, точки и heatmap при любом zoom и размере окна.

### Где менять

- `include/MapManager.h`: world-pixel helpers;
- `src/MapManager.cpp`: tile placement и point placement;
- `src/GuiManager.cpp`: drag и zoom around cursor;
- `src/main.cpp`: проверка выхода новой точки за экран.

### Реализация

Ввести функции:

```cpp
double lonToWorldX(double lon, int zoom) {
    return lonToTileX(lon, zoom) * 256.0;
}

double latToWorldY(double lat, int zoom) {
    return latToTileY(lat, zoom) * 256.0;
}
```

Экранная позиция:

```cpp
double centerX = lonToWorldX(centerLon, zoom);
double centerY = latToWorldY(centerLat, zoom);
double pointX = lonToWorldX(point.lon, zoom);
double pointY = latToWorldY(point.lat, zoom);

double screenX = width * 0.5 + pointX - centerX;
double screenY = height * 0.5 + pointY - centerY;
```

1. Тайлы размещать по разности их world origin и center.
2. Точки и IDW pixel coordinates считать тем же способом.
3. Drag изменяет center world pixels на mouse delta, затем выполняется inverse
   conversion в lon/lat.
4. Перед zoom сохранить world/geographic coordinate под курсором, после zoom
   изменить center так, чтобы эта coordinate осталась под тем же пикселем.
5. Новую точку считать видимой по `screenX/screenY`, а не по градусным bounds.
6. Ограничить latitude диапазоном Web Mercator около `[-85.0511, 85.0511]`.

### Проверка

- точка с координатами центра находится в `(width/2, height/2)`;
- проверка проходит для zoom 1, 10, 16 и 18;
- изменение размера окна сохраняет центр;
- zoom под курсором сохраняет объект под курсором;
- pan на север/юг не искажает совмещение тайлов и точек.

### Готово, когда

Текущая координата визуально находится точно в центре окна, а все слои
используют одну проекцию.

## P1. Корректные фоновые workers

### Где менять

- `include/MapManager.h`: condition variables, stop flags, queues;
- `src/MapManager.cpp`: constructor/destructor, request и worker loops.

### Реализация

Для tile queue:

```cpp
std::condition_variable m_tileCv;

m_tileCv.wait(lock, [&] {
    return !m_workerRunning || !m_requestQueue.empty();
});
```

1. `requestTile()` помещает уникальный request и вызывает `notify_one()`.
2. Worker извлекает request под mutex, затем освобождает mutex до disk/network
   I/O.
3. После успеха или ошибки удалить pending key; retry policy хранить отдельно.
4. Для heatmap использовать отдельную CV и latest-request slot.
5. Destructor выставляет stop, вызывает `notify_all()` и join.
6. Зафиксировать единый порядок mutex либо не удерживать два mutex одновременно.
7. В HTTP 418 ветке обязательно вызвать CURL cleanup через RAII.

### Проверка

- новый request обрабатывается без polling delay;
- временная ошибка допускает повторный request;
- shutdown завершается при пустой и заполненной очереди;
- ThreadSanitizer не показывает races/deadlocks.

## P1. Версионная публикация heatmap в OpenGL

### Где менять

- `include/MapManager.h`: result/uploaded version;
- `src/MapManager.cpp`: result publication и render upload.

### Реализация

1. Worker публикует immutable result с `generation`.
2. GUI копирует/забирает result только при
   `result.generation > uploadedGeneration`.
3. При первом результате или изменении размера использовать `glTexImage2D`.
4. При том же размере использовать `glTexSubImage2D`.
5. После upload обновить `uploadedGeneration`.
6. Добавить `shutdownGL()`, вызываемый до уничтожения GL context.

Пример:

```cpp
if (result.generation != m_uploadedGeneration) {
    uploadHeatmap(result);
    m_uploadedGeneration = result.generation;
}
```

### Проверка

Счетчик upload равен числу опубликованных generations, а не числу кадров.

## P1. Сохранение heatmap

### Где менять

- `include/Config.h`: runtime root или отказ от неиспользуемой константы;
- `src/MapManager.cpp`: формирование пути и запись snapshot;
- `ssot/env/stage.md`: рабочая директория запуска.

### Реализация

Путь должен разделять параметры:

```text
build/heatmap_cache/z16/rsrp/earfcn-1650/
  center-54.982000-82.893000-w600-h600.png
```

1. Передавать build/runtime output directory через config или аргумент запуска.
2. Включить zoom, criterion, EARFCN, центр и размер.
3. Записывать локальный immutable pixel snapshot.
4. Для безопасной замены писать временный файл и выполнять rename.
5. Не сохранять результат устаревшей generation.

### Проверка

Два критерия и два EARFCN создают четыре разных файла и не перезаписываются.

## P1. Потокобезопасные snapshots GUI

### Где менять

- `include/ServerCore.h`;
- `src/ServerCore.cpp`;
- `src/GuiManager.cpp`.

### Реализация

1. Не возвращать reference на изменяемый vector после снятия lock.
2. Для небольшого bounded plot window возвращать value snapshot.
3. Для больших данных использовать
   `std::shared_ptr<const PlotSnapshot>`.
4. Обновлять snapshot copy-on-write в server thread.
5. Заменить `vector + erase(begin)` на deque/ring buffer.

Пример:

```cpp
std::shared_ptr<const PlotSnapshot> ServerCore::getPlotSnapshot() const {
    std::lock_guard<std::mutex> lock(m_plotMutex);
    return m_plotSnapshot;
}
```

### Проверка

ThreadSanitizer при одновременном receive/render не сообщает data races.

## P1. Полные графики сигнала

Выполняется после задачи multiple LTE cells.

1. Вести bounded series по `(EARFCN, PCI)`.
2. Добавить tabs или selectable metric: RSRP, RSSI, RSSNR.
3. В legend выводить PCI и EARFCN.
4. Использовать общий X sample index/time.
5. Добавлять gap/NaN, если конкретная сота отсутствует в measurement.
6. Ограничить видимое окно, например последними 1000 samples.

Готово, когда контрольный поток с двумя PCI показывает две различимые линии для
каждого поддержанного критерия.

## P2. JSON и ZMQ

### Реализация

1. `parseJson()` должен возвращать success/error, а не пустой Measurement.
2. Проверять обязательные location и array types.
3. Логировать краткую причину без падения процесса.
4. Некорректный JSON не добавлять в history/DB.
5. Ответ сделать структурированным:

```json
{"status":"ok","stored":true,"receivedCells":2}
```

Ошибка:

```json
{"status":"error","message":"invalid lteCells"}
```

6. Разобрать GSM/NR/traffic либо явно исключить их из текущего scope.
7. JSON backup должен сохранять исходный payload или полную нормализованную
   запись со всеми сотами.
8. Удалить недостижимую строку после `return true`.
9. Обработать исключения context/socket/bind/send.

## P2. Надежность PostgreSQL

1. Читать host, port, database, user и password из environment/config.
2. Не использовать фиксированный `deviceIdEsc[256]`; device ID тоже передавать
   параметром `PQexecParams`.
3. Использовать transaction для measurement и cells.
4. При неуспешном connect вызывать `PQfinish` и обнулять pointer.
5. Добавить reconnect/backoff либо явный offline mode.
6. Не задерживать ZMQ REP длительным DB I/O: выделить bounded persistence queue
   или четко ограничить timeout.
7. Перенести downsampling в SQL, например через window function или spatial
   grid, вместо загрузки 50000 rows и шага в C++.

## P2. Ограниченный cache тайлов

1. Установить предел CPU/GPU cache, например 256 тайлов.
2. Хранить last-used frame/time и реализовать LRU.
3. Удалять GL texture в GUI-потоке при eviction.
4. После upload очищать `rgbaData`, если повторный upload не нужен.
5. `clearCache()` должен очистить queue, requested set и GL resources.
6. Не выполнять disk cache deletion из обычного memory clear без отдельной
   команды пользователя.

## P2. Штатный выход и проверка инициализации

1. Передать в `GuiManager` callback/flag выхода вместо `exit(0)`.
2. Проверять `SDL_Init`, `SDL_CreateWindow`, `SDL_GL_CreateContext`, `glewInit`,
   ImGui backend init и `server.start()`.
3. При частичной ошибке выполнять обратный cleanup только созданных ресурсов.
4. Порядок завершения:

```text
stop receive/workers
delete OpenGL textures
shutdown ImGui/ImPlot
destroy GL context/window
SDL_Quit
```

## P3. Мертвый код и warnings

После завершения P0/P1 проверить usage и удалить либо подключить:

- `generateHeatmapTexture()`;
- `drawCircle()` и `getColorForValue()`;
- `metersToPixels()`;
- `GuiManager::requestHeatmapUpdate()` и `generateHeatmap()`;
- `heatmapDirty`, `m_heatmapRequested`, `m_historyVersion`;
- устаревшие history methods.

В CMake убрать явное повторное подключение `imgui`, если оно уже транзитивно
приходит через `implot`, сохранив корректный порядок линковки.

Vendored warning stb не исправлять локальным патчем без необходимости. Его можно
принять для macOS compile-check либо обновить stb отдельной осознанной задачей.

## Общая матрица приемки

### Dev

- чистая Release-сборка;
- Debug-сборка;
- `git diff --check`;
- по возможности ASan/UBSan;
- ThreadSanitizer для потоковых изменений.

### Stage

- Android отправляет валидный и невалидный JSON;
- ZMQ всегда получает ответ;
- PostgreSQL хранит все LTE-соты;
- GUI показывает несколько PCI;
- центр карты совпадает с текущей точкой;
- pan/zoom визуально не разрывает слои;
- heatmap корректна для четырех критериев и разных EARFCN;
- PNG сохраняются раздельно;
- отключение сети и БД не приводит к зависанию;
- приложение штатно закрывается из меню и системной кнопкой.

## Рекомендуемые этапы

1. DB/RSSI и multiple-cell schema.
2. Plot snapshots и графики по PCI.
3. Web Mercator.
4. IDW correctness и versioning.
5. Workers и GPU upload.
6. Heatmap files и bounded caches.
7. JSON/ZMQ/DB reliability.
8. Shutdown, cleanup и удаление мертвого кода.
