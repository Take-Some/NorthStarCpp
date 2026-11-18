# NorthStarCpp — единая структура исходников и результатов сборки

17 ноября 2026. Архитектурная миграция CMake/CLI/QA, без удаления старых артефактов.

## Единый контракт каталогов

```text
NorthStarCpp/
├─ CMakeLists.txt                 единственная точка входа CMake
├─ CMakePresets.json              Debug / Release / RelWithDebInfo
├─ engine/northstarcpp/
│  ├─ modules/                    ЕДИНЫЙ исходный код движка и приложений
│  │  ├─ api/include/             исходные публичные заголовки
│  │  ├─ host/                     runtime и services
│  │  ├─ editor/                   Qt/Win32 editor
│  │  ├─ render/                   рендер-провайдеры
│  │  ├─ physics/                  физические провайдеры
│  │  └─ ...                       другие подсистемы
│  └─ resources/                  исходные общие ресурсы движка
├─ sdk/vendor/                    исходники и бинарные SDK третьих сторон
├─ tools/                         скрипты, CMake-модули, инструменты разработки
├─ tests/                         исходники тестов и фикстуры
├─ Projects/                      проекты игр (НЕ выход сборки движка)
└─ out/                           ЕДИНЫЙ корень генерируемых данных
   ├─ build/                      ЕДИНСТВЕННЫЙ CMakeCache.txt и build.ninja
   │  ├─ *.exe, *.dll              Player, Qt Editor, Project Manager, плагины
   │  ├─ lib/                      статические/import-библиотеки
   │  ├─ test_modules/             тестовые плагины, не production
   │  ├─ archive/                  northstar_shared.nsp
   │  ├─ generated/                CMake-generated headers/shaders/MOC
   │  ├─ engine/, sdk/             объектные и промежуточные файлы
   │  └─ compile_commands.json     индексация C++ в IDE
   ├─ logs/                       логи CLI
   ├─ qa/                         CTest/QA HTML, JSON, JUnit и снимки
   └─ package/
      ├─ qa/                      непубликуемые QA-дистрибутивы
      ├─ release/                 утверждённые релизные пакеты
      ├─ games/                   пакеты игровых проектов
      └─ install/                 staging cmake --install по умолчанию
```

Публичные бинарные SDK и provider DLLs не записываются в sdk/vendor и
engine/northstarcpp/modules. В SDK/vendor остаются только vendor-исходники,
подтверждённые внешние предсобранные зависимости и SDK — они не являются
результатом сборки собственных целей движка.

## Единственное CMake-дерево

Все версии сборки используют физически ОДИН каталог `out/build`. Для
смены Debug/Release CMake обновляет конфигурацию и пересобирает зависящие
от флагов targets. Debug и Release НЕ собираются одновременно.

```powershell
cmake --preset release
cmake --build --preset release --parallel 6
ctest --preset release --output-on-failure
```

Для Debug:

```powershell
cmake --preset debug
cmake --build --preset debug --parallel 6
ctest --preset debug --output-on-failure
```

Через CLI:

```powershell
.\northstarcpp.cmd configure --build-type Release
.\northstarcpp.cmd build --target all --build-type Release
.\northstarcpp.cmd run --target player --build-type Release
```

Если запущен Ninja, НЕ переключать конфигурацию одновременно с текущей
сборкой. `require_cache` у Python CLI обнаруживает неправильный
CMAKE_BUILD_TYPE и предлагает переконфигурировать вместо запуска
несоответствующих бинарников.

По умолчанию CMake отклоняет иные каталоги:
`out/gcc-release`, `out/gcc-debug`, `out/project-manager-qt`,
`out/distribution/gcc-*`, in-source builds. Для особых внешних CI
можно явно включить `-DNORTHSTARCPP_ALLOW_NONCANONICAL_BUILD=ON`;
это исключение, а не рекомендованный workflow.

## Сборка и дистрибуция — разные операции

`out/build` — рабочая компиляция и test files. Пакеты создаются
отдельно из зарегистрированных CMake targets:

```powershell
python -B -m tools.build_dist --qa-only --build-type Release
```

Вывод: `out/package/qa`. Публикация в `out/package/release`
остаётся доступна ТОЛЬКО после уже существующих проверок подписей,
лицензий и clean-machine acceptance, через `--promote-qa`.

`tools/build_release.cmd` больше не создаёт корневую
`distributable/`; команда запускает проверочную QA-упаковку.

Сборка Project Manager по `northstarcpp.cmd` использует тот же
`out/build`, что и Player. Генератор Abby Demo ссылается на
`out/build/northstarcpp_player.exe`, не на старые QA-копии.

## Совместимость и очистка

Ранее созданные каталоги **не удалялись и не перемещались**. Они могут
содержать пользовательские данные, релизные артефакты и отчёты.
Старые `CMakeCache.txt` не считаются источниками актуального engine
binary; все новые сборки выполняются через `out/build`.

Проверки политики:

```powershell
python -m unittest discover -s tests -p test_build_layout.py -v
python tools/verify_architecture.py --json
cmake --list-presets
```

Нельзя проверять корректность итогового EXE только по успешной
конфигурации или синтаксическим тестам. Окончательная приёмка требует
полной Release-сборки, CTest и запуска Qt Editor / Player.
