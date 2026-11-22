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

bool Lesson::begin(fight::FoitDuel* duel, UiSink* ui, ReadText read, const std::string& scriptName, int difficulty) {
    end();
    started_ = false;
    if (!duel || !duel->valid()) return false;
    access_ = std::make_unique<DuelAccess>(duel, difficulty);
    if (!host_.load(access_.get(), ui, std::move(read), scriptName)) return false;
    duel_ = duel;
    duel_->setPreStep([this](uint16_t& p1, uint16_t& p2) { onPads(p1, p2); });
    return true;
}

void Lesson::end() {
    if (duel_) {
        duel_->setPreStep(nullptr);
        duel_->setRules(0, {}, false);
        duel_->setRules(1, {}, false);
    }
    duel_ = nullptr;
}

void Lesson::onPads(uint16_t& p1, uint16_t& p2) {
    if (!duel_) return;
    if (!started_ && duel_->fighting()) {
        started_ = true;
        host_.start();
    }
    if (started_) host_.tick();
    bool scripted = false;
    const InputFrame in = host_.takeInput(&scripted);
    if (host_.p1Disabled()) p1 = 0;
    if (scripted) p2 = padFromFrame(in, duel_->body(1).facingRight());
    duel_->setRules(0, host_.forbiddenTags(0), !host_.blockingAllowed(0));
    duel_->setRules(1, host_.forbiddenTags(3), !host_.blockingAllowed(3));
}

}  // namespace script
}  // namespace zengine
