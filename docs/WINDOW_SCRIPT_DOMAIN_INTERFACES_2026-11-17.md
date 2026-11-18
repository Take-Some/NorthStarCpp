# NorthStarCpp — типизированные домены ScriptRuntime

Дата: 17 ноября 2026.
Репозиторий: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp.
Статус: полная CMake-сборка успешна, целевой CTest 20/20, hardware E2E успешен. Полная регрессия не закрыта.

## Архитектура

Центральный window_script_bindings.cpp больше не включает window_app_internal.hpp и не содержит определение App; это App-независимый координатор регистрации. Единственное определение App::configure_script_bindings() вынесено в window_script_bindings_host.cpp.

Шесть C++17 typed callback-контрактов определены в window_script_domain_contracts.hpp: Scene — 15 методов, Camera — 23, ECS — 8, Input — 5, Player — 12, Audio — 6. Всего 69. Контракты имеют фиксированные наборы method names, проверяют отсутствующие, неизвестные, пустые и дублированные callbacks.

Отдельные App-независимые единицы компиляции:
- window_script_scene_bindings.cpp
- window_script_camera_bindings.cpp
- window_script_ecs_bindings.cpp
- window_script_input_bindings.cpp
- window_script_player_bindings.cpp
- window_script_audio_bindings.cpp

Полностью вне App реализованы 10 обработчиков с узкими state views: Scene.objectCount(), Camera.available(), ECS.entities(), Input.isDown/mouseDown/pointerDelta/wheelDelta/gamepad, Player.motion(), Audio.supportsVoices(). Остальные 59 методов шести доменов остаются App-зависимыми host-callbacks, но их регистрация и проверка схемы осуществляется в независимых TU. Другие 35 методов engine.* остаются в host, 4 метода engine.save — в ранее выделенном window_script_save_bindings.cpp. Сумма: 108 исходных привязок сохранены.

Typed views для Scene и Camera используют функции чтения текущего состояния; ECS — current_scene accessor; Input — указатели и длины массивов клавиатуры, мыши, геймпадов; Player — два указателя движения; Audio — указатель на управляемый AudioEngine с прежней ленивой инициализацией. В JS указатели не экспортируются. Срок жизни view ограничен ScriptRuntime/host session.

## Уменьшение App header

window_app_state.hpp содержит приватный базовый тип AppRuntimeState, в который перенесены поля App с сохранением порядка инициализации/разрушения. App теперь наследует AppRuntimeState; его конструктор вызывает базовый конструктор с Options и startup report.

Размер window_app_internal.hpp: 37 665 -> 28 614 байт. window_app_state.hpp: 9 650 байт. Новый независимый coordinator window_script_bindings.cpp: 797 байт. Полный App host остаётся крупным (window_script_bindings_host.cpp: 103 471 байт); полноценный PIMPL не заявляется.

Миграционные скрипты (одноразовые, с проверкой повторного запуска):
- tools/dev/extract_window_app_state.py
- tools/dev/split_window_script_domains.py
- tools/dev/extract_window_script_state_views.py

Резервные копии оригиналов: out/refactor_backups.

## Проверки

Полный CMake Release build: cmake --build out/build --parallel 4 — exit 0. Компилировались Player, Qt Editor, все 6 независимых доменных модулей и suite test executables.

Выбранные профильные CTest: 20/20 PASS. В том числе northstarcpp_window_script_bindings_modularity, northstarcpp_window_script_domain_contracts, scripting ABI, SDK скрипты, camera script, ADS и northstarcpp_fps_weapon_effects.

tests/window_script_bindings_modularity_smoke.py — строгая проверка всех 69 контрактных методов, 108 общих регистраций и отсутствия App-заголовка во всех независимых регистраторах.

tests/window_script_domain_contracts_smoke.cpp — проверяет шесть C++17 typed domain sets, отсутствие дубликатов, неизвестных методов, пустых callbacks, неполных таблиц. Проверки не используют assert (не отключаются в Release).

tests/window_script_save_bindings_e2e.py — на настоящем D3D12 Player проверяет успешную загрузку всех шести доменов, контрактные ответы Input/Scene/Camera/ECS/Player/Audio, четыре метода engine.save и корректное завершение script lifecycle. PASS после полной сборки.

Дополнительный FPS-профиль AbbyPistolDemo на D3D12 запрашивал 1000 кадров, но игра завершилась с кодом 0 после 682 кадров. Этот профиль НЕ валиден для 1000-кадрового сравнения. Нет доказанного выигрыша FPS от декомпозиции.

## Ограничения

Полный CTest всего репозитория не подтверждён зелёным. Ранее WARP-тесты падали с Windows 0xC0000005; есть устаревшие тестовые фикстуры и зависимые от sharedAssets проверки. В сборках сохраняются нефатальные MinGW Boost RTTI warnings. Параллельные изменения asset-каталогов приводили к CMake glob updates и предупреждениям Ninja; необходим фиксированный снимок для сопоставимого профилирования.

Главный оставшийся долг — 59 App-зависимых обработчиков Scene/Camera/ECS/Player/Audio. Для них нужна последующая миграция в специализированные capability services с явной обработкой invalidate/unload/restart/destroy. Не требуется расширять публичный Script API.

Правки сохранены локально: без commit/push, без замены production dist.
