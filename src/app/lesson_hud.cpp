// lesson_hud.cpp - see lesson_hud.h.
#include "lesson_hud.h"

#include <algorithm>
#include <cmath>

#include "lesson_data.h"
#include "ui.h"

namespace zengine {

namespace {

constexpr Col kLhPanel{10, 12, 26, 210};
constexpr Col kLhEdge{120, 130, 170, 255};
constexpr Col kLhInk{235, 235, 245, 255};
constexpr Col kLhDim{160, 165, 185, 255};
constexpr Col kLhAccent{255, 214, 92, 255};
constexpr Col kLhDone{110, 220, 120, 255};

int cpCount(const std::string& s) {
    int n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

}  // namespace

std::vector<std::string> wrapLines(const std::string& s, int cols) {
    std::vector<std::string> out;
    if (cols < 4) cols = 4;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        const std::string para = s.substr(pos, nl - pos);
        pos = nl + 1;
        if (para.find_first_not_of(" \t\r") == std::string::npos) {
            out.push_back("");
            continue;
        }
        std::string line;
        size_t i = 0;
        while (i < para.size()) {
            while (i < para.size() && para[i] == ' ') ++i;
            size_t j = i;
            while (j < para.size() && para[j] != ' ') ++j;
            std::string word = para.substr(i, j - i);
            i = j;
            if (word.empty()) continue;
            while (cpCount(word) > cols) {   // a word longer than a line is cut
                size_t cut = 0;
                int n = 0;
                while (cut < word.size() && n < cols) {
                    ++cut;
                    while (cut < word.size() && (static_cast<unsigned char>(word[cut]) & 0xC0) == 0x80) ++cut;
                    ++n;
                }
                if (!line.empty()) {
                    out.push_back(line);
                    line.clear();
                }
                out.push_back(word.substr(0, cut));
                word = word.substr(cut);
            }
            if (line.empty()) line = word;
            else if (cpCount(line) + 1 + cpCount(word) <= cols) line += " " + word;
            else {
                out.push_back(line);
                line = word;
            }
        }
        if (!line.empty()) out.push_back(line);
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

void LessonHud::reset() {
    rows_.clear();
    maxRows_ = 5;
    first_ = 0;
    explKey_.clear();
    detailsKey_.clear();
    explActive_ = detailsOn_ = missionDone_ = false;
    explAge_ = clock_ = 0;
    slowMo_ = 0;
    fade_ = 0;
}

void LessonHud::clearAll() {
    rows_.clear();
    first_ = 0;
}

void LessonHud::add(const std::string& key, int progress, int maxProgress) {
    Row r;
    r.key = key;
    r.progress = progress;
    r.max = maxProgress;
    rows_.push_back(std::move(r));
}

void LessonHud::update(int index, const std::string& key, int progress, int maxProgress) {
    if (index < 1 || index > static_cast<int>(rows_.size())) return;
    Row& r = rows_[static_cast<size_t>(index - 1)];
    if (!key.empty()) r.key = key;
    r.progress = progress;
    r.max = maxProgress;
}

void LessonHud::complete(int index, bool done) {
    if (index < 1 || index > static_cast<int>(rows_.size())) return;
    rows_[static_cast<size_t>(index - 1)].done = done;
}

void LessonHud::showExplanation(bool on) {
    explActive_ = on;
    if (on) explAge_ = 0;
}

void LessonHud::screenFade(float amount, int /*dir*/, float alpha) {
    fade_ = std::clamp(amount, 0.0f, 1.0f) * std::clamp(alpha, 0.0f, 1.0f);
}

void LessonHud::pressAny() {
    if (!explActive_ || explAge_ < 18) return;
    explActive_ = false;
}

int LessonHud::doneRows() const {
    int n = 0;
    for (const Row& r : rows_) n += r.done ? 1 : 0;
    return n;
}

void LessonHud::tick() {
    ++clock_;
    if (explActive_) ++explAge_;
}

std::string LessonHud::text(const std::string& key, int progress, int maxProgress) const {
    std::string t = tr_ ? tr_(key) : std::string();
    if (t.empty()) t = key;
    return lessons::formatText(t, progress, maxProgress);
}

void LessonHud::draw(Ui& ui, int below) {
    if (fade_ > 0.001f) ui.fill(Rect{0, 0, 640, 360}, Col{0, 0, 0, static_cast<uint8_t>(std::lround(255.0f * fade_))});

    // ---- objectives (top left, under the life bars)
    if (!rows_.empty() && !explActive_) {
        constexpr int kX = 8, kW = 236, kCols = 26;
        int y = below + 4;
        const int firstRow = std::clamp(first_, 0, std::max(0, static_cast<int>(rows_.size()) - maxRows_));
        std::vector<std::vector<std::string>> lines;
        int height = 6;
        for (int i = firstRow; i < static_cast<int>(rows_.size()) && i < firstRow + maxRows_; ++i) {
            const Row& r = rows_[static_cast<size_t>(i)];
            std::string t = text(r.key, r.progress, r.max);
            std::string tk = tr_ ? tr_(r.key) : std::string();
            if (r.max > 1 && tk.find("{1}") == std::string::npos) t += " (" + std::to_string(r.progress) + "/" + std::to_string(r.max) + ")";
            lines.push_back(wrapLines(t, kCols));
            height += static_cast<int>(std::min<size_t>(lines.back().size(), 3)) * 10 + 4;
        }
        ui.fill(Rect{kX, y, kW, height}, kLhPanel);
        ui.outline(Rect{kX, y, kW, height}, kLhEdge);
        y += 5;
        for (size_t k = 0; k < lines.size(); ++k) {
            const Row& r = rows_[static_cast<size_t>(firstRow) + k];
            const Col c = r.done ? kLhDone : kLhInk;
            ui.outline(Rect{kX + 5, y, 8, 8}, c);
            if (r.done) ui.fill(Rect{kX + 7, y + 2, 4, 4}, c);
            const size_t n = std::min<size_t>(lines[k].size(), 3);
            for (size_t l = 0; l < n; ++l) ui.text(kX + 18, y + static_cast<int>(l) * 10, lines[k][l], c);
            y += static_cast<int>(n) * 10 + 4;
        }
    }

    // ---- hint line (bottom)
    if (detailsOn_ && !detailsKey_.empty() && !explActive_) {
        const std::vector<std::string> ls = wrapLines(text(detailsKey_), 72);
        const int h = static_cast<int>(std::min<size_t>(ls.size(), 3)) * 10 + 8;
        const int y = 358 - h - 4;
        ui.fill(Rect{60, y, 520, h}, kLhPanel);
        ui.outline(Rect{60, y, 520, h}, kLhEdge);
        for (size_t i = 0; i < ls.size() && i < 3; ++i) ui.text(320 - Ui::textW(ls[i]) / 2, y + 4 + static_cast<int>(i) * 10, ls[i], kLhInk);
    }

    // ---- explanation page (centre)
    if (explActive_ && !explKey_.empty()) {
        std::string full = text(explKey_);
        std::string title, body;
        const size_t nl = full.find('\n');
        if (nl == std::string::npos) {
            body = full;
        } else {
            title = full.substr(0, nl);
            body = full.substr(nl + 1);
            while (!body.empty() && (body.front() == '\n' || body.front() == '\r')) body.erase(body.begin());
        }
        constexpr int kW = 500, kCols = 59;
        const std::vector<std::string> ls = wrapLines(body, kCols);
        const int bodyLines = static_cast<int>(std::min<size_t>(ls.size(), 16));
        const int h = 18 + (title.empty() ? 0 : 26) + bodyLines * 10 + 26;
        const int x = 320 - kW / 2, y = std::max(below + 4, 180 - h / 2);
        ui.fill(Rect{x, y, kW, h}, Col{8, 10, 22, 235});
        ui.outline(Rect{x, y, kW, h}, kLhAccent);
        ui.outline(Rect{x + 2, y + 2, kW - 4, h - 4}, kLhEdge);
        int ty = y + 12;
        if (!title.empty()) {
            const int sc = Ui::textW(title, 2) <= kW - 24 ? 2 : 1;
            const std::string t = Ui::fit(title, kW - 24);
            ui.text(320 - Ui::textW(t, sc) / 2, ty, t, kLhAccent, sc);
            ty += 26;
        }
        for (int i = 0; i < bodyLines; ++i) ui.text(x + 14, ty + i * 10, ls[static_cast<size_t>(i)], kLhInk);
        if ((clock_ / 24) % 2 == 0 || explAge_ < 18) {
            const std::string p = tr_ && !tr_("* PRESS ANY BUTTON *").empty() ? tr_("* PRESS ANY BUTTON *") : "Press any button";
            ui.text(320 - Ui::textW(p) / 2, y + h - 16, p, kLhDim);
        }
    }
}

}  // namespace zengine
