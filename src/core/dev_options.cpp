#include "dev_options.h"

#include <algorithm>
#include <cstring>

#include "imgscale.h"

namespace zengine {
namespace devopt {

namespace {
const Def kDefs[kCount] = {
    {"ZD_DEV_RES", "res", 4, {"Low", "Medium", "High", "Native"}, 3, "Render resolution"},
    {"ZD_DEV_TEXQ", "texq", 4, {"Low", "Medium", "High", "Maximum"}, 2, "Sprite quality"},
    {"ZD_DEV_FILTER", "filter", 2, {"Pixelated", "Smooth"}, 1, "Smoothing"},
    {"ZD_DEV_SHADOW", "shadow", 5, {"Off", "Low", "Medium", "High", "Game"}, 4, "Shadows"},
    {"ZD_DEV_STAGE", "stage", 3, {"Auto", "Portrait", "Generated"}, 0, "Stage backdrop"},
    {"ZD_DEV_UPSCALE", "upscale", 3, {"Off", "Smooth", "Enhanced"}, 2, "Backdrop upscaling"},
    {"ZD_DEV_PRELOAD", "preload", 3, {"Minimal", "Standard", "Full"}, 1, "Frame preloading"},
    {"ZD_DEV_FPS", "fps", 2, {"Off", "On"}, 0, "FPS counter"},
    {"ZD_DEV_BOXES", "boxes", 2, {"Off", "On"}, 0, "Hit / hurt boxes"},
    {"ZD_DEV_INTRO", "intro", 2, {"Off", "On"}, 1, "Round intro"},
    {"ZD_DEV_ENTRANCE", "entrance", 2, {"Off", "On"}, 1, "Fighter entrances"},
    {"ZD_DEV_INFO", "info", 2, {"Off", "On"}, 0, "Stage info overlay"},
    {"ZD_DEV_RAM", "ram", 2, {"Off", "On"}, 0, "RAM display"},
};

bool eqi(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
        const char y = b[i] >= 'A' && b[i] <= 'Z' ? static_cast<char>(b[i] + 32) : b[i];
        if (x != y) return false;
    }
    return true;
}

struct Str {
    const char* key;
    const char* en;
    const char* es;
};
const Str kText[] = {
    {"dev", "Developer Options", "Opciones de Desarrollador"},
    {"dev.desc", "Resolution, sprite quality, shadows, stage backdrop and debug switches.",
     "Resolución, calidad de sprites, sombras, escenario e interruptores de depuración."},
    {"dev.res", "Render resolution", "Resolución de render"},
    {"dev.res.d", "How many pixels the picture is drawn with before it fills the window: Low 33%, Medium 50%, High 75%, Native 100%. Lower is faster.",
     "Con cuántos píxeles se dibuja la imagen antes de llenar la ventana: Baja 33%, Media 50%, Alta 75%, Nativa 100%. Menos = más rápido."},
    {"dev.texq", "Sprite quality", "Calidad de sprites"},
    {"dev.texq.d", "How much of the fighters' art is kept: Low = quarter size, Medium = half, High = as much as the screen can show, Maximum = every pixel.",
     "Cuánto del arte de los luchadores se conserva: Baja = un cuarto, Media = la mitad, Alta = lo que la pantalla puede mostrar, Máxima = todos los píxeles."},
    {"dev.filter", "Smoothing", "Suavizado"},
    {"dev.filter.d", "Smooth: sprites and stage are filtered and placed on sub-pixel positions. Pixelated: hard pixel edges.",
     "Suave: sprites y escenario filtrados y colocados con precisión de subpíxel. Pixelado: bordes duros."},
    {"dev.shadow", "Shadows", "Sombras"},
    {"dev.shadow.d", "Off, Low (flat blob), Medium (soft blob), High (the fighter's own flattened silhouette) or Game (the silhouette fading with height like the PC game).",
     "Apagadas, Bajas (mancha plana), Medias (mancha suave), Altas (la silueta aplastada del luchador) o Juego (la silueta que se desvanece con la altura como en PC)."},
    {"dev.stage", "Stage backdrop", "Fondo del escenario"},
    {"dev.stage.d", "Auto: the stage's own 2D texture, else a generated stage. Portrait: the Character Select picture. Generated: always the generated stage.",
     "Auto: la textura 2D del escenario, si no un escenario generado. Portada: la imagen del selector. Generado: siempre el escenario generado."},
    {"dev.upscale", "Backdrop upscaling", "Escalado del fondo"},
    {"dev.upscale.d", "For stage pictures smaller than the screen: Off, Smooth (plain filter) or Enhanced (bicubic 2x + light sharpening, done while loading).",
     "Para fondos más pequeños que la pantalla: Apagado, Suave (filtro simple) o Mejorado (bicúbico 2x + enfoque ligero, hecho al cargar)."},
    {"dev.preload", "Frame preloading", "Precarga de cuadros"},
    {"dev.preload.d", "Frames turned into textures behind the loading screen: Minimal (idle, walk), Standard (+ jumps, dashes, normals), Full (every frame, longer load).",
     "Cuadros convertidos en texturas tras la pantalla de carga: Mínima (reposo, caminar), Estándar (+ saltos, dashes, normales), Completa (todos, carga más larga)."},
    {"dev.fps", "FPS counter", "Contador de FPS"},
    {"dev.fps.d", "Shows the frame rate in a corner of the match.", "Muestra los cuadros por segundo en una esquina del combate."},
    {"dev.boxes", "Hit / hurt boxes", "Cajas de golpe / daño"},
    {"dev.boxes.d", "Draws each frame's push (blue), hurt (green) and hit (red) boxes.", "Dibuja las cajas de empuje (azul), daño (verde) y golpe (rojo) de cada cuadro."},
    {"dev.intro", "Round intro", "Intro de ronda"},
    {"dev.intro.d", "The \"Take N / Action!\" card before each round.", "La tarjeta \"Toma N / ¡Acción!\" antes de cada ronda."},
    {"dev.entrance", "Fighter entrances", "Entradas de luchadores"},
    {"dev.entrance.d", "Each fighter's entrance animation at the start of a match.", "La animación de entrada de cada luchador al empezar el combate."},
    {"dev.info", "Stage info overlay", "Info del escenario"},
    {"dev.info.d", "Prints which backdrop is used, its size, the render size and the sprite level on screen.",
     "Muestra qué fondo se usa, su tamaño, el tamaño de render y el nivel de sprites en pantalla."},
    {"dev.ram", "RAM display", "Pantalla de RAM"},
    {"dev.ram.d", "Seven-segment readout of the memory the app uses (resident MB, peak, growth per minute) and the match's state hash, bottom right. Error codes show there on their own.",
     "Lectura de siete segmentos de la memoria que usa la app (MB residentes, pico, crecimiento por minuto) y el hash del combate, abajo a la derecha. Los códigos de error salen ahí solos."},
    {"dev.back", "Back", "Atrás"},
    // choice names
    {"c.Low", "Low", "Baja"},
    {"c.Medium", "Medium", "Media"},
    {"c.High", "High", "Alta"},
    {"c.Native", "Native", "Nativa"},
    {"c.Maximum", "Maximum", "Máxima"},
    {"c.Pixelated", "Pixelated", "Pixelado"},
    {"c.Smooth", "Smooth", "Suave"},
    {"c.Off", "Off", "No"},
    {"c.On", "On", "Sí"},
    {"c.Game", "Game", "Juego"},
    {"c.Auto", "Auto", "Auto"},
    {"c.Portrait", "Portrait", "Portada"},
    {"c.Generated", "Generated", "Generado"},
    {"c.Enhanced", "Enhanced", "Mejorado"},
    {"c.Minimal", "Minimal", "Mínima"},
    {"c.Standard", "Standard", "Estándar"},
    {"c.Full", "Full", "Completa"},
};
}  // namespace

const Def& def(Id id) { return kDefs[std::clamp<int>(id, 0, kCount - 1)]; }

int indexOf(Id id, const std::string& stored) {
    const Def& d = def(id);
    for (int i = 0; i < d.nChoices; ++i)
        if (eqi(stored, d.choices[i])) return i;
    return d.def;
}

int Settings::spriteLevel(double pxPerArtPx) const {
    if (spriteFullSize) return 0;
    return spriteLevelFor(pxPerArtPx, spriteMinLevel);
}

Settings resolve(const std::function<std::string(const char* key)>& get) {
    Settings s;
    auto idx = [&](Id id) { return indexOf(id, get ? get(def(id).key) : std::string()); };
    static const double kRes[] = {0.33, 0.50, 0.75, 1.0};
    s.renderScale = kRes[idx(kRenderRes)];
    const int q = idx(kSpriteQuality);   // Low, Medium, High, Maximum
    s.spriteMinLevel = q == 0 ? 2 : q == 1 ? 1 : 0;
    s.spriteFullSize = q == 3;
    static const size_t kMB = 1024u * 1024u;
    static const size_t kCache[] = {48 * kMB, 96 * kMB, 160 * kMB, 320 * kMB};
    s.spriteCacheBytes = kCache[q];
    s.smooth = idx(kSmoothing) == 1;
    s.shadow = static_cast<ShadowMode>(idx(kShadows));
    s.stage = static_cast<StageSource>(idx(kStage));
    s.upscale = static_cast<Upscale>(idx(kUpscale));
    s.preload = static_cast<Preload>(idx(kPreload));
    s.fps = idx(kFps) == 1;
    s.boxes = idx(kBoxes) == 1;
    s.roundIntro = idx(kRoundIntro) == 1;
    s.entrances = idx(kEntrances) == 1;
    s.stageInfo = idx(kStageInfo) == 1;
    s.ramDebug = idx(kRam) == 1;
    return s;
}

std::string menuIni() {
    std::string t = "TopText: @own:dev\n";
    for (int i = 0; i < kCount; ++i) {
        const Def& d = kDefs[i];
        t += "Option:\nType: Setting\nText: @own:dev.";
        t += d.name;
        t += "\nDescription: @own:dev.";
        t += d.name;
        t += ".d\n";
        for (int c = 0; c < d.nChoices; ++c) {
            t += c == d.def ? "DefaultChoice: " : "Choice: ";
            t += d.choices[c];
            t += "\n";
        }
        t += "Keyword: ";
        t += d.key;
        t += "\n";
    }
    return t;
}

const char* text(const std::string& key, bool spanish) {
    for (const Str& s : kText)
        if (key == s.key) return spanish ? s.es : s.en;
    return nullptr;
}

}  // namespace devopt
}  // namespace zengine
