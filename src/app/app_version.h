// app_version.h - the version the loader shows (the splash).  android/app/build.gradle (versionName) carries the same string.
#pragma once

namespace zengine {
inline constexpr const char* kAppVersion = "0.0.0.0-Dev";   // 1.0.0.0 will be the main release; until then no dates in the name
}
