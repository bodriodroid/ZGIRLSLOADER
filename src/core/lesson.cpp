// lesson.cpp - see lesson.h.
#include "lesson.h"

namespace zengine {
namespace script {

uint16_t padFromFrame(const InputFrame& f, bool facingRight) {
    using namespace fight;
    const uint16_t fwd = facingRight ? kRight : kLeft;
    const uint16_t back = facingRight ? kLeft : kRight;
    uint16_t pad = 0;
    switch (f.dir) {
        case 0: pad = fwd; break;
        case 1: pad = static_cast<uint16_t>(kDown | fwd); break;
        case 2: pad = kDown; break;
        case 3: pad = static_cast<uint16_t>(kDown | back); break;
        case 4: pad = back; break;
        case 5: pad = static_cast<uint16_t>(kUp | back); break;
        case 6: pad = kUp; break;
        case 7: pad = static_cast<uint16_t>(kUp | fwd); break;
        default: break;   // 8: neutral
    }
    pad = static_cast<uint16_t>(pad | ((f.buttons & 0x3Fu) << 4));   // LP MP HP LK MK HK = fight.h bits 4..9
    return pad;
}

bool Lesson::begin(fight::FoitDuel* duel, UiSink* ui, ReadText read, const std::string& scriptName) {
    duel_ = nullptr;
    started_ = false;
    if (!duel || !duel->valid()) return false;
    access_ = std::make_unique<DuelAccess>(duel);
    if (!host_.load(access_.get(), ui, std::move(read), scriptName)) return false;
    duel_ = duel;
    return true;
}

void Lesson::step(uint16_t playerPad, uint16_t dummyPad) {
    if (!duel_) return;
    if (!started_ && duel_->fighting()) {
        started_ = true;
        host_.start();
    }
    if (started_) host_.tick();
    bool scripted = false;
    const InputFrame in = host_.takeInput(&scripted);
    const uint16_t p1 = host_.p1Disabled() ? static_cast<uint16_t>(0) : playerPad;
    const uint16_t p2 = scripted ? padFromFrame(in, duel_->body(1).facingRight()) : dummyPad;
    duel_->step(p1, p2);
}

}  // namespace script
}  // namespace zengine
