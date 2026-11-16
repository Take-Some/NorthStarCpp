# NorthStarCpp — V-HACD Auto Convex и Collision Response Matrix
Дата: 2026-11-15

## Реализовано
- Vendored V-HACD 4.1 (BSD-3-Clause) в sdk/vendor/sources/VHACD; отдельная статическая библиотека northstarcpp_collision_cooker. Upstream архивирован, но библиотека выполняет настоящую воксельную декомпозицию.
- splitsNum 1–16 задаёт максимум оболочек, не обязательное точное число. Разрешение по умолчанию — 100000 вокселей.
- Авторинг OBJ/YDD/YDR через действующий AssetStore. В компоненте northstar.physics.collider.hulls (NCH1) сохраняются треугольники каждой выпуклой оболочки, исходный путь и splitsNum. Валидация границ и индексов, совместимость со старыми сценами.
- Jolt загружает готовые оболочки через независимое расширение NORTHSTARCPP_PHYSICS_COOKED_HULLS_MODULE_ABI, без повторной V-HACD декомпозиции при запуске. Старые сцены без cache готовятся при запуске.
- Preview Hull (All / Hull 1 / Hull 2 / ...) — в Details и Prefab Collision Editor. Выделенный объект показывает фактический каркас готовых convex hulls.
- Collider V3 хранит Object Type и шесть независимых Collision Responses: WorldStatic, WorldDynamic, Pawn, PhysicsBody, Vehicle, Destructible; Ignore/Overlap/Block. Версии V1/V2 читаются с настройками Block по умолчанию.
- Jolt: эффективное отклонение Ignore на уровне ObjectLayerPairFilter; Overlap реализован через ContactSettings.mIsSensor; Block создаёт обычные контакты. Доступны события Begin/End через PhysicsScene::poll_collision_events(). В C ABI основная physics ABI 4 не менялась, добавлены независимые расширения.
- Bullet исключён из сборки в текущей QA-конфигурации, исходники не удалены.

## Проверено
- V-HACD cook smoke: PASS, 2 hulls и NCH1 round-trip, exit 0.
- Jolt physics_jolt.dll: компиляция/линковка PASS.
- physics_smoke (Jolt): PASS, включая cached hulls, Ignore/Overlap/Block, Overlap End, exit 0.
- editor_prefab_smoke: PASS, включая перенос V3 Collision Matrix и сохранённых hulls через prefab instances, exit 0.
- Qt editor window/setup, viewport, asset preview: компиляция PASS.
- Полная сборка Qt editor не завершена: сторонний runtime V8 15.8.62 не скачивается (SSL CA cert error 77); визуальная проверка не проводилась.

## Ограничения
- V-HACD 4.1 не гарантирует ровно splitsNum частей и не тождественен конкретной версии алгоритма Unreal.
- Cook пока синхронный; нужны background jobs, прогресс, отмена, DDC.
- Каналы фиксированные (6), пользовательские UE-style каналы/пресеты не добавлены.
- Матрица работает для контактов rigid body; Channel-aware raycast/sweep в публичном query API ещё не реализованы.
- Не проводились GPU screenshots, масштабная стресс-регрессия, профилирование и проверка установленного production редактора.
- Коммит, push и замена production DLL/EXE не выполнялись.
