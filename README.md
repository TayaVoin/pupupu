# Dear ImGui проект
Проект для изучения библиотеки Dear ImGui.

## Что внутри
- Подключение Dear ImGui и Implot через git submodules
- Сборка через CMake
- Интеграция с SDL2 и OpenGL3
- Базовое окно с кнопкой и счетчиком

## Структура
```
├── CMakeLists.txt
├── src/
│   └── main.cpp
├── third_party/
│   ├── imgui/
│   └── implot/
└── README.md
```
## Стек технологий
- C++17
- Dear ImGui (ветка docking) — библиотека для создания GUI.
- Implot — дополнение к Dear ImGui для визуализации графиков.
- SDL2 — Simple DirectMedia Layer (используется для создания окна, обработки ввода).
- OpenGL / GLEW — графический API и библиотека для управления его расширениями.
- CMake — инструмент для автоматизации сборки.
- Git Submodules — для управления зависимостями.

## Сборка
```
# Установка зависимостей (Ubuntu/Debian)
sudo apt install libsdl2-dev libgl1-mesa-dev libglew-dev

# Клонирование с субмодулями
git clone --recursive <ссылка>
cd <папка>

# Сборка
mkdir build && cd build
cmake ..
make
./main
```
