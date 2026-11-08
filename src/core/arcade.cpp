// arcade.cpp - see arcade.h.
#include "arcade.h"

#include <algorithm>

namespace zengine {
namespace arcade {

namespace {
// xorshift32: deterministic for a given seed on every platform.
struct Rng {
    uint32_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    size_t below(size_t n) { return n ? static_cast<size_t>(next() % n) : 0; }
};

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}

void shuffle(std::vector<std::string>& v, Rng& r) {
    for (size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[r.below(i)]);
}
}  // namespace

int Ladder::difficultyFor(int baseDifficulty, int index, bool boss) {
    if (boss) return 2;
    return std::clamp(baseDifficulty - 1 + index / 3, 0, 2);
}

void Ladder::begin(const std::string& player, const std::vector<std::string>& roster, const std::vector<std::string>& stages,
                   const std::string& boss, const std::string& bossStage, int baseDifficulty, uint32_t seed) {
    fights_.clear();
    idx_ = continues_ = 0;
    finished_ = false;
    Rng rng{seed ? seed : 0x9E3779B9u};

    // opponents: the roster without the player and without the boss; when that is too few the player's character may come up too,
    // and when even that is not enough the pool is dealt again (never the same fighter twice in a row)
    std::vector<std::string> pool;
    for (const std::string& r : roster) {
        const std::string k = lower(r);
        if (k == lower(player) || k == lower(boss)) continue;
        if (std::find_if(pool.begin(), pool.end(), [&](const std::string& p) { return lower(p) == k; }) == pool.end()) pool.push_back(r);
    }
    if (static_cast<int>(pool.size()) < kRandomFights && !player.empty()) pool.push_back(player);
    if (pool.empty()) pool.push_back(player);

    std::vector<std::string> opp;
    std::vector<std::string> deck;
    while (static_cast<int>(opp.size()) < kRandomFights) {
        if (deck.empty()) {
            deck = pool;
            shuffle(deck, rng);
            if (!opp.empty() && deck.size() > 1 && lower(deck.back()) == lower(opp.back())) std::swap(deck.back(), deck.front());   // dealt from the back
        }
        opp.push_back(deck.back());
        deck.pop_back();
    }

    // stages: random, not the boss's, not the one before
    std::vector<std::string> sp;
    for (const std::string& s : stages)
        if (lower(s) != lower(bossStage)) sp.push_back(s);
    std::string prev;
    for (int i = 0; i < kRandomFights; ++i) {
        Fight f;
        f.fighter = opp[static_cast<size_t>(i)];
        if (!sp.empty()) {
            std::vector<std::string> cand;
            for (const std::string& s : sp)
                if (sp.size() < 2 || s != prev) cand.push_back(s);
            f.stage = cand[rng.below(cand.size())];
            prev = f.stage;
        }
        f.difficulty = difficultyFor(baseDifficulty, i, false);
        fights_.push_back(f);
    }
    Fight b;
    b.fighter = boss;
    b.stage = bossStage;
    b.difficulty = difficultyFor(baseDifficulty, kRandomFights, true);
    b.rounds = kBossRounds;
    b.boss = true;
    fights_.push_back(b);
}

const Fight& Ladder::current() const {
    static const Fight none;
    if (fights_.empty()) return none;
    return fights_[static_cast<size_t>(std::clamp(idx_, 0, total() - 1))];
}

bool Ladder::won() {
    if (!active() || finished_) return finished_;
    if (idx_ >= total() - 1) {
        finished_ = true;
        return true;
    }
    ++idx_;
    return false;
}

}  // namespace arcade
}  // namespace zengine
