# Frame Window Counter v10.3 — Android64 Mobile Port + Windows (GD 2.2081 / Geode v5.11.2)

Кроссплатформенная сборка **Frame Window Counter v10.3** для **Android64** и **Windows**.

## Что сделано для Android64 (Mobile Port)
- **GitHub Actions (`.github/workflows/build.yml`)**: добавлена автоматическая сборка **`build-android64`** (`ubuntu-latest`, `platform: android64`, `sdk: v5.11.2`) параллельно со сборкой под Windows — после пуша в **Actions** сразу появятся оба готовых `.geode`.
- **Совместимость с Clang / Android NDK (libc++ / OpenGL ES 2.0)**:
  - Явно добавлены строгие заголовки (`<cstddef>`, `<cstdint>`, `<cmath>`).
  - Проверки `m_anticheatSpike` и `FMODAudioEngine::m_system` переведены в шаблоны (`template`), чтобы Clang не падал на платформах с отличающимися биндингами.
  - Win32 / OpenGL desktop вызовы в `InternalRenderer.hpp` полностью изолированы под `#ifdef GEODE_IS_WINDOWS`.
- **Адаптация под телефоны**:
  - Безопасные отступы HUD (`18px`) от закруглённых краёв и выреза камеры.
  - На баннере `Scan Exact` вместо `ESC` отображается `Press Back / Pause to cancel`.
  - Вкладка `Render` на Android показывает подсказку по записи экрана, а кнопка `Folder` в `Audio` показывает точный путь к папке кастомных звуков на телефоне.
