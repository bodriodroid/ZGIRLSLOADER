// zd_features.h - switches for parts of ZDroid that are built but not exposed yet.
#pragma once

namespace zengine {

// Fight mode (the Play screen behind Arcade / CPU Quick Match / Local Versus / Training) is switched off while the menu and the
// UI get finished. The code stays in the build; configure with -DZDROID_FIGHT_MODE=ON to bring it back.
#if defined(ZDROID_FIGHT_MODE) || defined(__ANDROID__)   // ZGirlsMobile: always on in the APK
constexpr bool kFightEnabled = true;
#else
constexpr bool kFightEnabled = false;
#endif

// While the loader is being tested every story node, every character and every gallery picture starts unlocked, so each Story
// can be walked through from the first scene.  Set to false to play the real progression (ProgressionInit.txt locks + the .savini).
constexpr bool kUnlockAllForTesting = true;

}  // namespace zengine
