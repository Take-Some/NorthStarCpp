# NorthStarCpp — Frame Builder, RuntimeServices, DDC and Tasks
Дата: 2026-11-17. Базовый HEAD в начале: e6be4509.
Сборка: _qa_frame_pipeline_20261117, Release, Ninja, MinGW-w64 GCC 16.2.

## 1. Viewport Frame Builder и GPU submission

Созданы:
- `modules/viewport/viewport_frame_builder.hpp/.cpp`: отдельно компилируемая индексация активных Entity, материалов, процедурных компонентов, категорий поверхностей и FPS visual modes. Поколения кадров исключают устаревшие ссылки; повторное использование hash-узлов сокращает потенциальные аллокации. Ссылки на строки и компоненты действуют только в пределах текущего синхронного draw().
- `modules/viewport/viewport_frame_submission.hpp/.cpp`: отдельно компилируемая отправка ID-picking и Material ABI v1/v2, shader-reload key association. Возвращает типизированные статусы ошибок, сохраняя исходные диагностические сообщения.
- `tests/viewport_frame_builder_smoke.cpp` и `tests/viewport_frame_submission_smoke.cpp`: stale-entity, precedence, malformed component, FPS mode, no-pick clearing, missing v2 provider.
- В `viewport.cpp` и CMake подключены новые модули. Ранее созданный `viewport_frame_scratch.hpp` продолжает повторно использовать GPU staging буферы.

Не перенесены: крупные участки подготовки геометрии и skinned-пакетов, полное владение жизненным циклом GPU. Публичный ABI рендера, разрешение провайдеров и качество мешей не менялись.

## 2. RuntimeServices / AppState

Созданы:
- `modules/editor/window_runtime_services.hpp/.cpp`: отдельный сервис разрешения startup visual prefab с атомарным применением на уровне сцены и определения script/FPS mode.
- `modules/editor/window_runtime_app_state.hpp`: AppSimulationState для PIE pause/eject/stepper без дополнительных heap allocations.

Изменены `window_runtime_bootstrap.inc`, `window_runtime_win32_messages.inc`, `window_runtime_frame_loop.inc`, `window.cpp`, CMake. App по-прежнему имеет единый frame clock и является владельцем сцены, физики, renderer и исходных Options. Полный Pimpl/перевод всех .inc в независимые единицы компиляции еще не выполнен.

## 3. Unified DDC / bounded Task Scheduler

Созданы:
- `modules/api/include/northstarcpp/derived_data_cache.hpp`: namespaced/versioned content key, общий KeyBuilder, bounded blob read и атомарная публикация через временный файл.
- `modules/api/include/northstarcpp/task_scheduler.hpp`: ограниченный пул рабочих потоков (2 worker, очередь до 64), приоритеты, FIFO внутри приоритета, cancellation, std::future и управление ошибками.
- `tests/unified_ddc_tasks_smoke.cpp`: key domain/version, hit/miss и размер, cancel, priority/backpressure.

Изменены `collision_cook_cache.hpp` (общие функции DDC с сохранением старых ключей .nch), `qt_collision_cook_task.hpp` (V-HACD ставится в ограниченную очередь; Qt GUI опрашивает future без блокировки; при заполнении очереди показывает warning).

Пока общим DDC пользуется collision cooking. Импорт NEF8 и компиляция shader variants к новому DDC НЕ подключены. Dependency TaskGraph пока не реализован — это bounded executor.

## 4. Попутное исправление V8 startup

В двух `bootstrap.js` для sharedAssets исправлено только некорректное regex для файлов .nui, ломавшее загрузку V8. `node --check` прошёл на обеих копиях. До исправления три Player tests выдавали `Numeric separators are not allowed at the end of numeric literals`; после rebuild прошли.

## 5. Проверки

В отдельной конфигурации `_qa_frame_pipeline_20261117` успешно скомпилированы и слинкованы northstarcpp_player.exe, northstarcpp_editor_qt.exe, все новые smoke и внешние test_geometry.dll fixtures.

Итог выбранного CTest: **18/18 PASS**:
unified_ddc_tasks, collision_vhacd, pie_command_state,
qt_editor_mouse_look, qt_editor_native_ui, qt_editor_authoring, qt_editor_gpu_surface,
runtime_frame_scheduler, viewport_frame_submission, viewport_frame_builder,
viewport_frame_scratch, player_provider_inspection, player_bundled_scripting,
player_scene_runtime, player_prefab_runtime, qt_editor_player_launch,
player_scene_validation, player_reject_invalid_scene.

Дополнительный тест `northstarcpp_player_pistol_runtime` — **FAIL**:
`Pistol runtime collider is missing` (`scripts/main.js:24`). Отдельный ранее наблюдавшийся дефект физического префаба; проблема НЕ решена в этом рефакторинге.

`git diff --check`: exit 0, предупреждения только LF/CRLF.

## 6. Ограничения / релиз

Нет замеров реального повышения FPS, p95/p99, выделений памяти на кадр или тестов D3D12/Vulkan GPU parity; нельзя заявлять конкретный прирост. Полная CTest-регрессия и стресс-тесты не выполнены. Источники Unreal не копировались. Много файлов изменялось в рабочем дереве параллельно; чужие изменения не сбрасывались. Production dist не обновлялся; commit и push не выполнялись.

Следующие задачи: GPU visual acceptance, тест жизненного цикла AppState и временных ссылок, полное App RuntimeServices/Pimpl, NEF8/shader DDC, TaskGraph зависимости, исправление pistol collider, A/B профилирование steady-state 1000+ кадров.
