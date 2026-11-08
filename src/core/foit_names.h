// foit_names.h - the vocabulary of the .foit script slots.
//
// A character body owns kSlotCount (177) float "slots"; scripts read and write them by id.  The ids below are the ones the
// game's script text (the .spt sources the .foit files are compiled from) uses: it spells a slot by name and the compiler turns
// the name into the id stored in the chain operands.  The list was read out of the shipped executable's script-text parser
// (names and ids only, nothing else of it is used here).  Slots 0, 85 and 168..176 have no name in the text language.
//
// Named constants are float literals the text language also knows (PHYSICSAIR, MIDHITANIM, the flag bit names ...).
#pragma once
#include <cstddef>
#include <cstdint>

namespace zengine {
namespace foit {
namespace slot {
constexpr int NUMHITS = 1;
constexpr int NUMBLOCKS = 2;
constexpr int NUMTOUCHES = 3;
constexpr int HITSTUN = 53;
constexpr int HITTYPE = 4;
constexpr int HITPAUSE = 52;
constexpr int ONGROUND = 5;
constexpr int GRD = 5;
constexpr int GOTHIT = 6;
constexpr int BLOCKED = 7;
constexpr int ENEMYATTACKING = 8;
constexpr int THROWING = 9;
constexpr int THROWN = 10;
constexpr int MYCOMBOCOUNT = 11;
constexpr int THEIRCOMBOCOUNT = 12;
constexpr int TOUCHSCREENLR = 13;
constexpr int TOUCHSCREENUD = 14;
constexpr int CAMEDGER = 16;
constexpr int CAMEDGEL = 15;
constexpr int CAMEDGEU = 17;
constexpr int CAMEDGED = 18;
constexpr int WALLSTOPDELTA = 19;
constexpr int ENEMYAHEAD = 20;
constexpr int LastHitCannotKill = 21;
constexpr int INFINITEFOUND = 22;
constexpr int DEBUG1 = 37;
constexpr int UNDIZZYFALLOFFTIMER = 36;
constexpr int TOTALSTUN = 38;
constexpr int STATENUM = 39;
constexpr int PREVSTATENUM = 40;
constexpr int PREVVISIBLESTATENUM = 41;
constexpr int DISALLOWTAGS = 43;
constexpr int HIGHLOWUNBLKTIMER = 44;
constexpr int ARMORCANCELTIME = 42;
constexpr int MYNAME = 47;
constexpr int POINTNAME = 48;
constexpr int ATTACKERNAME = 51;
constexpr int ENEMYNAME = 50;
constexpr int PREVPOINTNAME = 49;
constexpr int WEIGHT = 54;
constexpr int FRICTION = 55;
constexpr int FACINGDIR = 56;
constexpr int FACINGTOENEMY = 23;
constexpr int FACINGTOATTACKER = 24;
constexpr int PLAYERNUM = 25;
constexpr int TEAMPOS = 26;
constexpr int WASCH = 70;
constexpr int WASHITBYASSIST = 94;
constexpr int ISPOINT = 27;
constexpr int ASSISTOK = 28;
constexpr int ASSISTCALLXOFFSET = 29;
constexpr int ROUNDOVER = 30;
constexpr int POINTX = 31;
constexpr int POINTY = 32;
constexpr int POINTFACING = 33;
constexpr int POINTASSISTLOCK = 34;
constexpr int SPUN = 35;
constexpr int TETHEREDTO = 57;
constexpr int TETHERPT = 45;
constexpr int SKIPPED = 58;
constexpr int PHYSICSTYPE = 59;
constexpr int BLOCKTYPE = 60;
constexpr int INVINCIBLE = 61;
constexpr int SPRITESTATE = 62;
constexpr int CAMSHAKE = 63;
constexpr int LIFEBAR = 64;
constexpr int REDLIFE = 65;
constexpr int SUPERBAR = 66;
constexpr int EXTRADAMAGE = 67;
constexpr int DAMAGEMULT = 68;
constexpr int ASSISTDMGMOD = 69;
constexpr int SUPERFLASH = 71;
constexpr int SUPERSHADOWS = 72;
constexpr int PALFXSELF2TIME = 76;
constexpr int PALFXSELF2RATE = 46;
constexpr int PALFXSELF2 = 75;
constexpr int PALFXSELFTIME = 74;
constexpr int PALFXSELF = 73;
constexpr int PALFXHITTIME = 78;
constexpr int PALFXHIT = 77;
constexpr int PALFXSTATUS = 79;
constexpr int FX32BLEND = 82;
constexpr int FX32FRAME = 81;
constexpr int FX32 = 80;
constexpr int DAMAGESCALE = 83;
constexpr int DAMAGE = 84;
constexpr int CHIPDAMAGE = 86;
constexpr int STUNDAMAGE = 87;
constexpr int BASESTUN = 88;
constexpr int BASEMETERGAIN = 92;
constexpr int METERGAIN = 89;
constexpr int NOCOUNTERS = 90;
constexpr int AIRHINT = 91;
constexpr int ANIMHINT = 93;
constexpr int PUSHBACKTIME = 95;
constexpr int PUSHBACKDELTA = 97;
constexpr int PUSHBACKALWAYS = 98;
constexpr int PUSHBACK = 96;
constexpr int LIGHTRAD = 103;
constexpr int LIGHTR = 99;
constexpr int LIGHTG = 100;
constexpr int LIGHTB = 101;
constexpr int LIGHTI = 102;
constexpr int LIGHTX = 104;
constexpr int LIGHTY = 105;
constexpr int LIGHTZ = 106;
constexpr int FAKESPARKX = 108;
constexpr int FAKESPARKY = 109;
constexpr int FAKESPARKSOUND = 110;
constexpr int FAKESPARK = 107;
constexpr int THROWPT = 114;
constexpr int THROWADDHITS = 115;
constexpr int THROWADDDMG = 116;
constexpr int THROWCAMXPCT = 117;
constexpr int THROWCAMYPCT = 118;
constexpr int THROWFATAL = 119;
constexpr int THROWFINALIZE = 120;
constexpr int BEAMSTRETCH = 121;
constexpr int ARTROTATION = 122;
constexpr int ARMORHITS = 123;
constexpr int THROWTECHTIME = 129;
constexpr int THROWUNBLKFIXTIMER = 130;
constexpr int THROWX = 112;
constexpr int THROWY = 113;
constexpr int THROW = 111;
constexpr int JUMPSLEFT = 124;
constexpr int DASHESLEFT = 125;
constexpr int NORMALJUMP = 126;
constexpr int CHAINLV = 127;
constexpr int AIRDASHLOCKOUT = 128;
constexpr int COMBOSTARTEDATMAXUNDIZZY = 131;
constexpr int COUNTERHIT = 132;
constexpr int CANCELOK = 133;
constexpr int DHCNUM = 134;
constexpr int DOPOSE = 135;
constexpr int IPSSTAGE = 136;
constexpr int DHCINFOTIMER = 137;
constexpr int PBGCTIMER = 138;
constexpr int USEALTFRAMES = 139;
constexpr int HOLDTIMER = 140;
constexpr int INPUTLAG = 141;
constexpr int INTERACT = 143;
constexpr int EXTRAGRDOFFSET = 142;
constexpr int ISPROJECTILE = 144;
constexpr int ISBIGCHARACTER = 145;
constexpr int MODIFIERMOTIONAGE = 146;
constexpr int TAGTO = 147;
constexpr int TAGINFO = 148;
constexpr int ASSISTLOCKNOW = 149;
constexpr int ASSISTREADY = 150;
constexpr int PLAYASSISTMOTION = 151;
constexpr int SOUNDSLOT = 152;
constexpr int PALETTEOPTIONSET = 153;
constexpr int LINECOLORR = 154;
constexpr int LINECOLORG = 155;
constexpr int LINECOLORB = 156;
constexpr int LINECOLORMIX = 157;
constexpr int XVEL = 158;
constexpr int YVEL = 159;
constexpr int XPOS = 160;
constexpr int YPOS = 161;
constexpr int SOLIDFLOOR = 162;
constexpr int SOLIDWALLS = 163;
constexpr int XSCALE = 164;
constexpr int YSCALE = 165;
constexpr int ALPHA = 166;
constexpr int MAXREPEATS = 167;
constexpr int PROJECTGROUND = 159;
constexpr int CORNER = 158;
constexpr int CHECKFRAMES = 83;
constexpr int HITFRAMES = 53;
constexpr int MISSFRAMES = 52;
constexpr int HITSTATUS = 84;
}  // namespace slot

// Primary name of a slot id (the first spelling above), nullptr when the text language has none.
inline const char* slotName(int id) {
    switch (id) {
        case 1: return "NUMHITS";
        case 2: return "NUMBLOCKS";
        case 3: return "NUMTOUCHES";
        case 4: return "HITTYPE";
        case 5: return "ONGROUND";
        case 6: return "GOTHIT";
        case 7: return "BLOCKED";
        case 8: return "ENEMYATTACKING";
        case 9: return "THROWING";
        case 10: return "THROWN";
        case 11: return "MYCOMBOCOUNT";
        case 12: return "THEIRCOMBOCOUNT";
        case 13: return "TOUCHSCREENLR";
        case 14: return "TOUCHSCREENUD";
        case 15: return "CAMEDGEL";
        case 16: return "CAMEDGER";
        case 17: return "CAMEDGEU";
        case 18: return "CAMEDGED";
        case 19: return "WALLSTOPDELTA";
        case 20: return "ENEMYAHEAD";
        case 21: return "LastHitCannotKill";
        case 22: return "INFINITEFOUND";
        case 23: return "FACINGTOENEMY";
        case 24: return "FACINGTOATTACKER";
        case 25: return "PLAYERNUM";
        case 26: return "TEAMPOS";
        case 27: return "ISPOINT";
        case 28: return "ASSISTOK";
        case 29: return "ASSISTCALLXOFFSET";
        case 30: return "ROUNDOVER";
        case 31: return "POINTX";
        case 32: return "POINTY";
        case 33: return "POINTFACING";
        case 34: return "POINTASSISTLOCK";
        case 35: return "SPUN";
        case 36: return "UNDIZZYFALLOFFTIMER";
        case 37: return "DEBUG1";
        case 38: return "TOTALSTUN";
        case 39: return "STATENUM";
        case 40: return "PREVSTATENUM";
        case 41: return "PREVVISIBLESTATENUM";
        case 42: return "ARMORCANCELTIME";
        case 43: return "DISALLOWTAGS";
        case 44: return "HIGHLOWUNBLKTIMER";
        case 45: return "TETHERPT";
        case 46: return "PALFXSELF2RATE";
        case 47: return "MYNAME";
        case 48: return "POINTNAME";
        case 49: return "PREVPOINTNAME";
        case 50: return "ENEMYNAME";
        case 51: return "ATTACKERNAME";
        case 52: return "HITPAUSE";
        case 53: return "HITSTUN";
        case 54: return "WEIGHT";
        case 55: return "FRICTION";
        case 56: return "FACINGDIR";
        case 57: return "TETHEREDTO";
        case 58: return "SKIPPED";
        case 59: return "PHYSICSTYPE";
        case 60: return "BLOCKTYPE";
        case 61: return "INVINCIBLE";
        case 62: return "SPRITESTATE";
        case 63: return "CAMSHAKE";
        case 64: return "LIFEBAR";
        case 65: return "REDLIFE";
        case 66: return "SUPERBAR";
        case 67: return "EXTRADAMAGE";
        case 68: return "DAMAGEMULT";
        case 69: return "ASSISTDMGMOD";
        case 70: return "WASCH";
        case 71: return "SUPERFLASH";
        case 72: return "SUPERSHADOWS";
        case 73: return "PALFXSELF";
        case 74: return "PALFXSELFTIME";
        case 75: return "PALFXSELF2";
        case 76: return "PALFXSELF2TIME";
        case 77: return "PALFXHIT";
        case 78: return "PALFXHITTIME";
        case 79: return "PALFXSTATUS";
        case 80: return "FX32";
        case 81: return "FX32FRAME";
        case 82: return "FX32BLEND";
        case 83: return "DAMAGESCALE";
        case 84: return "DAMAGE";
        case 86: return "CHIPDAMAGE";
        case 87: return "STUNDAMAGE";
        case 88: return "BASESTUN";
        case 89: return "METERGAIN";
        case 90: return "NOCOUNTERS";
        case 91: return "AIRHINT";
        case 92: return "BASEMETERGAIN";
        case 93: return "ANIMHINT";
        case 94: return "WASHITBYASSIST";
        case 95: return "PUSHBACKTIME";
        case 96: return "PUSHBACK";
        case 97: return "PUSHBACKDELTA";
        case 98: return "PUSHBACKALWAYS";
        case 99: return "LIGHTR";
        case 100: return "LIGHTG";
        case 101: return "LIGHTB";
        case 102: return "LIGHTI";
        case 103: return "LIGHTRAD";
        case 104: return "LIGHTX";
        case 105: return "LIGHTY";
        case 106: return "LIGHTZ";
        case 107: return "FAKESPARK";
        case 108: return "FAKESPARKX";
        case 109: return "FAKESPARKY";
        case 110: return "FAKESPARKSOUND";
        case 111: return "THROW";
        case 112: return "THROWX";
        case 113: return "THROWY";
        case 114: return "THROWPT";
        case 115: return "THROWADDHITS";
        case 116: return "THROWADDDMG";
        case 117: return "THROWCAMXPCT";
        case 118: return "THROWCAMYPCT";
        case 119: return "THROWFATAL";
        case 120: return "THROWFINALIZE";
        case 121: return "BEAMSTRETCH";
        case 122: return "ARTROTATION";
        case 123: return "ARMORHITS";
        case 124: return "JUMPSLEFT";
        case 125: return "DASHESLEFT";
        case 126: return "NORMALJUMP";
        case 127: return "CHAINLV";
        case 128: return "AIRDASHLOCKOUT";
        case 129: return "THROWTECHTIME";
        case 130: return "THROWUNBLKFIXTIMER";
        case 131: return "COMBOSTARTEDATMAXUNDIZZY";
        case 132: return "COUNTERHIT";
        case 133: return "CANCELOK";
        case 134: return "DHCNUM";
        case 135: return "DOPOSE";
        case 136: return "IPSSTAGE";
        case 137: return "DHCINFOTIMER";
        case 138: return "PBGCTIMER";
        case 139: return "USEALTFRAMES";
        case 140: return "HOLDTIMER";
        case 141: return "INPUTLAG";
        case 142: return "EXTRAGRDOFFSET";
        case 143: return "INTERACT";
        case 144: return "ISPROJECTILE";
        case 145: return "ISBIGCHARACTER";
        case 146: return "MODIFIERMOTIONAGE";
        case 147: return "TAGTO";
        case 148: return "TAGINFO";
        case 149: return "ASSISTLOCKNOW";
        case 150: return "ASSISTREADY";
        case 151: return "PLAYASSISTMOTION";
        case 152: return "SOUNDSLOT";
        case 153: return "PALETTEOPTIONSET";
        case 154: return "LINECOLORR";
        case 155: return "LINECOLORG";
        case 156: return "LINECOLORB";
        case 157: return "LINECOLORMIX";
        case 158: return "XVEL";
        case 159: return "YVEL";
        case 160: return "XPOS";
        case 161: return "YPOS";
        case 162: return "SOLIDFLOOR";
        case 163: return "SOLIDWALLS";
        case 164: return "XSCALE";
        case 165: return "YSCALE";
        case 166: return "ALPHA";
        case 167: return "MAXREPEATS";
        default: return nullptr;
    }
}

struct NamedConst {
    const char* name;
    float value;
};
inline constexpr NamedConst kNamedConsts[] = {
    {"PHYSICSAIR", 2.0f},
    {"PHYSICSGROUND", 3.0f},
    {"PHYSICSBOTH", 1.0f},
    {"PHYSICSLOCK", 4.0f},
    {"MIDHITANIM", 5.0f},
    {"LOWHITANIM", 6.0f},
    {"TRIPANIM", 1.0f},
    {"TECHABLESPRAWLANIM", 14.0f},
    {"TECHABLEKNOCKDOWNANIM", 15.0f},
    {"KNOCKBACKANIM", 16.0f},
    {"WALLSPLATANIM", 17.0f},
    {"FLOORBOUNCEANIM", 7.0f},
    {"HALFFLOORBOUNCEANIM", 20.0f},
    {"QUARTERFLOORBOUNCEANIM", 21.0f},
    {"CEILINGBOUNCEANIM", 22.0f},
    {"TECHABLEFLOORBOUNCEANIM", 19.0f},
    {"WALLBOUNCEANIM", 8.0f},
    {"SLIDINGANIM", 9.0f},
    {"NOOTGSLIDINGANIM", 24.0f},
    {"CRUMPLEANIM", 10.0f},
    {"STAGGERANIM", 11.0f},
    {"FREEZEANIM", 23.0f},
    {"SNAPOUT1ANIM", 12.0f},
    {"SNAPOUT2ANIM", 13.0f},
    {"LAUNCHERANIM", 18.0f},
    {"THROWHIT", 4.0f},
    {"ALLHITS", 23.0f},
    {"HIGHHIT", 1.0f},
    {"MIDHIT", 3.0f},
    {"LOWHIT", 2.0f},
    {"ALWAYSHIT", 16.0f},
    {"PROJECTILEHIT", 32.0f},
    {"PASSTHROUGHVICTIMS", 64.0f},
    {"RENDERVICTIMINFRONT", 128.0f},
    {"THROWSANDMIDHITS", 7.0f},
    {"STATEGROUND", 2.0f},
    {"NORMALHINT", 0.0f},
    {"KNOCKDOWNHINT", 1.0f},
    {"FACINGLEFT", -1.0f},
    {"STAGEEDGER", 3750.0f},
    {"CAMSHAKET", 4.0f},
    {"CAMSHAKEL", 1.0f},
    {"CAMSHAKEM", 2.0f},
    {"CAMSHAKEH", 3.0f},
    {"CAMSHAKES", 5.0f},
    {"CAMSHAKEU", 6.0f},
    {"CAMSHAKEE", 7.0f},
    {"CAMSHAKEA", 8.0f},
    {"CAMSHAKEI", 9.0f},
    {"CAMSHAKEO", 10.0f},
    {"CANHITALLIES", 1.0f},
    {"CANBEHITBYALLIES", 2.0f},
    {"CANNOTHITENEMIES", 4.0f},
    {"CANNOTBEHITBYENEMIES", 8.0f},
    {"REPORTONLY", 16.0f},
    {"ALWAYSTRIGGERSUPERFINISH", 64.0f},
    {"UNTHROWABLE", 32.0f},
    {"IGNOREPARENTSUPERFLASH", 128.0f},
    {"IGNOREALLSUPERFLASHES", 4096.0f},
    {"USELEADERPALETTEFX", 256.0f},
    {"STEALLEADERTHROWVICTIM", 512.0f},
    {"USETHROWERHITBOXES", 1024.0f},
    {"EXTERNALASSISTLOCK", 2048.0f},
    {"MUTEMUSIC", 8192.0f},
    {"INHERITPARENTHITPAUSE", 16384.0f},
    {"WANTCAMCONTROL", 32768.0f},
    {"COUNTASSISTASUSED", 65536.0f},
    {"SpecialCaseDisplayLayer", 131072.0f},
    {"ProtectPointFromProjectiles", 262144.0f},
    {"PREBLOCKDISTANCE", 275.0f},
    {"ALLFLAGS", 0.0f},
    {"ONCEPERJUMPALL", 32.0f},
    {"ONCEPERCOMBOALL", 64.0f},
};
inline constexpr size_t kNamedConstCount = sizeof(kNamedConsts) / sizeof(kNamedConsts[0]);

}  // namespace foit
}  // namespace zengine
