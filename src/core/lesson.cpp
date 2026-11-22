// lesson.cpp - see lesson.h.
#include "lesson.h"

#include <algorithm>

#include "palfx.h"

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
    rules_ = Rules();
    locked_[0].clear();
    locked_[1].clear();
}

void Lesson::setRules(const Rules& r) {
    if (!duel_) return;
    rules_ = r;
    for (int s = 0; s < 2; ++s) {
        locked_[s].clear();
        for (const std::string& w : r.locked[s]) {
            if (w.empty() || w == "None") continue;
            const uint32_t h = palfx::nameHash(w);
            if (std::find(locked_[s].begin(), locked_[s].end(), h) == locked_[s].end()) locked_[s].push_back(h);
        }
        duel_->setStart(s, r.lifePct[s] == 100 ? 0 : r.lifePct[s], r.meterBars[s]);
        foit::rt::Object& o = duel_->bodyMut(s);
        lifeHold_[s] = o.life();
        if (r.lifeStart[s] >= 0.0f) o.life() = std::max(1.0f, lifeHold_[s] * std::min(1.0f, r.lifeStart[s]));
        meterHold_[s] = o.slots[66];   // SUPERBAR
    }
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
    std::vector<uint32_t> f0 = host_.forbiddenTags(0), f1 = host_.forbiddenTags(3);
    for (uint32_t h : locked_[0])
        if (std::find(f0.begin(), f0.end(), h) == f0.end()) f0.push_back(h);
    for (uint32_t h : locked_[1])
        if (std::find(f1.begin(), f1.end(), h) == f1.end()) f1.push_back(h);
    duel_->setRules(0, f0, !host_.blockingAllowed(0));
    duel_->setRules(1, f1, !host_.blockingAllowed(3));
    if (started_ && duel_->fighting()) {   // the challenge's life / meter modifiers
        for (int s = 0; s < 2; ++s) {
            foit::rt::Object& o = duel_->bodyMut(s);
            const float fullLife = lifeHold_[s] > 0 ? lifeHold_[s] : o.life();
            switch (rules_.lifeMod[s]) {
                case Rules::Mod::Regen: o.life() = std::min(fullLife, o.life() + fullLife * 0.0004f); break;    // about 2.4% of the bar a second
                case Rules::Mod::Drain: o.life() = std::max(1.0f, o.life() - fullLife * 0.0004f); break;
                case Rules::Mod::Hold: o.life() = fullLife; break;
                case Rules::Mod::None: break;
            }
            switch (rules_.meterMod[s]) {
                case Rules::Mod::Regen: o.slots[66] = std::min(500.0f, o.slots[66] + 0.05f); break;
                case Rules::Mod::Drain: o.slots[66] = std::max(0.0f, o.slots[66] - 0.05f); break;
                case Rules::Mod::Hold: o.slots[66] = meterHold_[s]; break;
                case Rules::Mod::None: break;
            }
        }
    }
}

}  // namespace script
}  // namespace zengine
