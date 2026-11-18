> Paths reflect the grouped OOP engine SDK layout. The canonical load order is api/manifest.json.

# NorthStarCpp — TypeScript SDK, модульный bootstrap и IntelliSense
Дата: 2026-11-17

## Исполнено

1. Добавлена локальная версия TypeScript 5.9.3 в tools/ts_sdk
   (package.json, package-lock.json, tsconfig.json); системный tsc не нужен.
2. В CTest включён строгий typecheck через локальный TypeScript и проверка
   TypeScript Language Service. Проверяются публичные декларации SDK
   и примеры декларативного UI.
3. Создан tools/ts_sdk/ts_language_service.cjs: постоянный процесс с протоколом
   JSON Lines, реальной проверкой типов TypeScript, сигнатурами и документацией.
   Исходный JS/TS код не исполняется; он передаётся из редактора как текст.
4. qt_text_asset_editor.cpp подключает один общий асинхронный TypeScript
   Language Service для всех вкладок. Запросы задерживаются на 75 мс,
   устаревшие ответы не применяются, базовый QCompleter по .d.ts сохранён
   как резервный вариант. Вставка методов сохраняет исходное имя переменной.
5. Тип UiDeclaration.props уточнён до UiProperties с распространёнными
   свойствами нативного UI (x, y, width, height, padding, gap, text,
   color, background и др.); расширяемые свойства разрешены.
6. Bootstrap исходники разделены на 12 файлов по подсистемам в
   engine/northstarcpp/resources/sharedAssets/api/ (domain directories listed in manifest.json).
   tools/dev/compose_northstar_bootstrap.py детерминированно собирает из них
   единый защищённый entrypoint. tools/dev/embed_engine_api.py делает это
   автоматически и не включает отдельные фрагменты в runtime дважды.

## Совместимость

Исходная декомпозиция bootstrap прошла побайтное сравнение:
размер 66541 байт, SHA-256:
916e9c81fe664f97b66a07d653d48b3532d3e94d5bc9bd7e0cfc2ec0258aaa6e.

Это самостоятельные исходные блоки, но в рантайме они сохраняют единое
лексическое пространство. Полный переход на независимо инстанцируемые
ESM/DI-модули потребует анализа циклических зависимостей. Контракт
защищённого NorthStar и загрузка проектов сейчас не изменены.

## Проверки

- TypeScript strict/noEmit: PASS
- JS/TS TypeScript Language Service: PASS
- Атомарность, перерисовка, события UI: PASS
- Qt QCompleter с реальным TypeScript: PASS
- Композиция SDK bootstrap: PASS
- CTest в out/build: 9/9 PASS
- Полная сборка northstarcpp_editor_qt.exe: PASS
- Размер построенного EXE: 12522168 байт

## Размещение

Сборка: out/build/northstarcpp_editor_qt.exe
TypeScript tooling: tools/ts_sdk/
Исходники защищённого SDK:
engine/northstarcpp/resources/sharedAssets/api/
Архитектура: engine/northstarcpp/resources/sharedAssets/api/core/MODULES.md

Для воспроизведения на другой машине:
npm ci --prefix tools/ts_sdk --ignore-scripts --no-audit --no-fund

## Ограничения

- TypeScript completion теперь семантический, но это ещё не полный LSP:
  диагностика в тексте, безопасное Rename Symbol и Code Actions не добавлены.
- Полный Qt-редактор удалось собрать после установки штатных пакетов
  MSYS2 mingw-w64-x86_64-boost и mingw-w64-x86_64-boost-libs версии 1.92.0-3.
- Линковщик сообщил о нескольких duplicate-section предупреждениях для
  boost::any в объектах, собранных с разными состояниями заголовков;
  рекомендуемая отдельная задача — чистая пересборка перед release.
- Производственный out/package/dist не заменён. Git commit/push не выполнены.
