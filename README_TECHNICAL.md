# Vulkan Meadow: техническая документация

Документ для разработчиков: требования к окружению, сборка и устройство проекта.

## Стек

- C11
- Vulkan 1.0
- GLFW 3
- GLSL и SPIR-V
- CMake 3.24+
- vcpkg для зависимости GLFW

## Требования

- Windows 10/11 и драйвер с поддержкой Vulkan
- Vulkan SDK от LunarG; компилятор шейдеров `glslc` должен быть доступен через `VULKAN_SDK`
- Visual Studio Build Tools с MSVC
- CMake 3.24 или новее
- vcpkg

## Сборка в PowerShell

Выполняйте команды из корневого каталога репозитория. Укажите путь к своей установке vcpkg:

```powershell
$env:VCPKG_ROOT = 'C:\dev\vcpkg'
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
.\build\Release\VulkanMeadow.exe
```

Во время конфигурации CMake vcpkg устанавливает GLFW согласно манифесту `vcpkg.json`. CMake также компилирует vertex- и fragment-шейдеры в SPIR-V в каталоге `build/shaders`.

## Структура

- `src/main.c` — создание Vulkan instance/device, swapchain, depth resources, рендер-проход и обработка ввода
- `shaders/meadow.vert`, `shaders/meadow.frag` — полноэкранный рендер рельефа, неба и освещения
- `shaders/meadow_plant.vert`, `shaders/meadow_plant.frag` — процедурная геометрия травы и цветов, освещение и затухание вдали
- `screens/` — скриншоты и референс
- `CMakeLists.txt` — цели сборки приложения и SPIR-V-шейдеров
- `vcpkg.json` — зависимости vcpkg

Описание игры и управление: [README.md](README.md).
