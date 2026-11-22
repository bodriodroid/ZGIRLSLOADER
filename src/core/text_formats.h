// sg_loader core - parsers for the game's plain-text data files.
// Formats were read off the user's own extracted files (toc.ini, stages.ini, music.ini,
// compile.ini, *.stm). All parsers are tolerant: unknown lines are skipped, never fatal.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zengine {

// Splits on \n, \r\n or \r. Keeps empty lines.
std::vector<std::string> splitLines(std::string_view text);
std::string trim(std::string_view s);
// Splits "a, b ,c" on commas and trims each part; empty parts are dropped.
std::vector<std::string> splitCommaList(std::string_view s);

// stages.ini: "<LocalizationId> <levelFile> [options...] <stagePicture>"  (# comments)
struct StageEntry {
    std::string locId;     // e.g. New_Meridian
    std::string level;     // e.g. new_meridian_3d
    std::string picture;   // e.g. stage_new_meridian
    std::vector<std::string> options;  // e.g. IsAlt>Arena, ExcludeFromRandom
};
std::vector<StageEntry> parseStages(std::string_view text);

// music.ini: "Track: <name>", optional "Stage: a, b", "Files: <intro>, <loop>"
struct MusicTrack {
    std::string name;
    std::vector<std::string> stages;
    std::vector<std::string> files;
};
std::vector<MusicTrack> parseMusic(std::string_view text);

// toc.ini: "File: Peacock.stm"
std::vector<std::string> parseToc(std::string_view text);

// compile.ini: pairs of "<x>.spt #input" / "<x>.foit #output" lines
struct CompileJob {
    std::string spt;
    std::string foit;
};
std::vector<CompileJob> parseCompile(std::string_view text);

// *.stm arcade/story timelines: "CharName X", then blocks starting with "ArcadeNode"/"StoryNode"
struct StmNode {
    std::string kind;  // "ArcadeNode" or "StoryNode"
    std::string arg;   // text after the kind on the same line ("Complete" for the end marker)
    std::vector<std::pair<std::string, std::string>> fields;
    const std::string* get(std::string_view key) const;
};
struct StmFile {
    std::string charName;
    std::string charIcon;   // "CharIcon Textures/Tutorials/symbol_filia01.png" (the trial / tutorial chapter lists)
    std::vector<StmNode> nodes;
};
StmFile parseStm(std::string_view text);

}  // namespace zengine
