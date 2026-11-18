# AbbyPistolDemo — воспроизводимое исправление запуска

Дата: 2026-11-17. Репозиторий: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp.

## Подтверждённые причины

1. `Projects/AbbyPistolDemo/run.cmd` указывал на удалённые временные QA-папки вместо `out/build/northstarcpp_player.exe`; `--shared-assets` использовал несуществующую `<root>/sharedAssets`. Исправлено на `engine/northstarcpp/resources/sharedAssets`.
2. Файл `Projects/AbbyPistolDemo/main.nscn` имел корректный NEF8-конверт, но внутри record `kind=7`, entity `44`, type `northstar.gameplay.skeletal_mesh` декларированная длина payload была 36 байт при фактической 47. Native decoder отклонял его с `Scene ListFile record contains trailing bytes`. Длина исправлена с полным сохранением 56 записей. Исходный бинарный файл сохранён в `out/qa/abby_main_before_nef8_length_fix.nscn.bak`.
3. `engine/northstarcpp/modules/host/scripting.cpp` не разрешал импорты `WeaponBallistics` и `WeaponEffects` из `@northstar/engine`, хотя они уже присутствовали в SDK. Оба добавлены в список допустимых экспортов.

## Защита от повторения

- `tools/dev/scene_component_length_check.py` проверяет declared/actual length каждого plugin component в NEF8; `--repair` исправляет только поля длины и сохраняет backup.
- `tools/create_abby_pistol_demo.py --verify` теперь проверяет не только оболочку NEF8, но и каждое поле длины ECS-компонента. Генератор нового проекта также использует действительный путь к общим ассетам.
- Native decoder при ошибке сообщает `kind`, `version`, `id` и число оставшихся байт.

## Проверки

- `cmake --build out/build --target northstarcpp_player -j 4` — PASS.
- `python tools/create_abby_pistol_demo.py --verify` — PASS; 56 NEF8 записей, 45 сущностей, 29 звуков NAC.
- `python -m tools.dev.scene_component_length_check Projects/AbbyPistolDemo/main.nscn` — PASS, 56 записей.
- `node tests/weapon_ballistics_shared_smoke.cjs` — PASS.
- `node tests/prefab_root_oop_smoke.cjs` — PASS.
- Реальный запуск `northstarcpp_player.exe --explore --scene <AbbyPistolDemo/main.nscn> --shared-assets <engine/northstarcpp/resources/sharedAssets>` с Jolt/D3D12 — процесс продолжал работать спустя 10 секунд. В логе: D3D12 materials, Abby native YDD ready, both wrist IK solved, `bone_attachment=PASS`. QA-процесс затем преднамеренно завершён.
- `git diff --check` для исправленных исходников — PASS.

## Остающееся

- Предупреждение: `pistol_shell.ydd` имеет один неразрешённый YMAT-слот, используется нейтральный PBR fallback. На запуск не влияет.
- Визуальная проверка HUD, выстрела и ADS конкретным пользователем ещё не проведена. Автоматизированный запуск подтверждён, но не заменяет продолжительную игровую сессию.
- Изменения локальные; commit, push и замена production dist не выполнялись.
