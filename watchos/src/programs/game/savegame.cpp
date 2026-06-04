#include "savegame.h"
#include "player.h"
#include "inventory.h"
#include <Preferences.h>
#include <string.h>

#define SAVE_MAGIC 0x44554E33u    // 'DUN3' — бампать при смене формата

struct SaveBlob {
    uint32_t magic;
    Player   player;
    Item     inv[INV_MAX];
    int      invCount;
    Item     equWeapon, equArmor;
    bool     hasWeapon, hasArmor;
    int32_t  px, py, campX, campY;
    bool     hasCamp;
};

static Preferences prefs;

bool saveExists()
{
    prefs.begin("dungeon", true);
    size_t n = prefs.getBytesLength("save");
    prefs.end();
    return n == sizeof(SaveBlob);
}

void gameSave(int32_t px, int32_t py, int32_t campX, int32_t campY, bool hasCamp)
{
    SaveBlob s;
    s.magic   = SAVE_MAGIC;
    s.player  = gp;
    memcpy(s.inv, inv, sizeof(s.inv));
    s.invCount  = invCount;
    s.equWeapon = equWeapon; s.equArmor = equArmor;
    s.hasWeapon = hasWeapon; s.hasArmor = hasArmor;
    s.px = px; s.py = py; s.campX = campX; s.campY = campY; s.hasCamp = hasCamp;
    prefs.begin("dungeon", false);
    prefs.putBytes("save", &s, sizeof(s));
    prefs.end();
}

bool gameLoad(int32_t &px, int32_t &py, int32_t &campX, int32_t &campY, bool &hasCamp)
{
    prefs.begin("dungeon", true);
    SaveBlob s;
    size_t n = prefs.getBytes("save", &s, sizeof(s));
    prefs.end();
    if (n != sizeof(s) || s.magic != SAVE_MAGIC) return false;
    gp = s.player;
    memcpy(inv, s.inv, sizeof(inv));
    invCount  = s.invCount;
    equWeapon = s.equWeapon; equArmor = s.equArmor;
    hasWeapon = s.hasWeapon; hasArmor = s.hasArmor;
    px = s.px; py = s.py; campX = s.campX; campY = s.campY; hasCamp = s.hasCamp;
    return true;
}
