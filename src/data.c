// Static game data plus small shared helpers (rng, number formatting, skills, inventory).
#include "game.h"

#define EQ(slot, req, a, s, d, val, tier, ex) {0, ex, 0, IF_EQUIP, slot, req, a, s, d, 0, val, tier}
// the fields after the icon, for ranged and magic gear
#define BOW(req, val, ratk) IF_EQUIP | IF_RANGED, EQ_WEAPON, req, 0, 0, 0, 0, val, TIER_WOOD, ratk, 0, 0
#define ARROW(req, val, rstr, tier) IF_EQUIP | IF_RANGED | IF_STACK, EQ_AMMO, req, 0, 0, 0, 0, val, tier, 0, rstr, 0
#define STAFF(a, s, val, matk) IF_EQUIP | IF_MAGIC, EQ_WEAPON, 1, a, s, 0, 0, val, TIER_WOOD, 0, 0, matk

const ItemDef items[I_COUNT] = {
    [I_NONE] = {"", "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    [I_COINS] = {"Coins", "Lovely money!", ICON_COINS, IF_STACK, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_LOGS] = {"Logs", "A number of wooden logs.", ICON_LOGS, IF_LIGHT, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_OAK_LOGS] = {"Oak logs", "Logs cut from an oak tree.", ICON_OAK_LOGS, IF_LIGHT, 0, 0, 0, 0, 0, 0, 20, 0},
    [I_WILLOW_LOGS] = {"Willow logs", "Logs cut from a willow tree.", ICON_WILLOW_LOGS, IF_LIGHT, 0, 0, 0, 0, 0, 0, 32, 0},
    [I_TINDERBOX] = {"Tinderbox", "Useful for lighting a fire.", ICON_TINDERBOX, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_BRONZE_AXE] = {"Bronze axe", "A woodcutter's axe.", ICON_BRONZE_AXE, IF_EQUIP, EQ_WEAPON, 1, -2, 2, 0, 0, 16, TIER_BRONZE},
    [I_IRON_AXE] = {"Iron axe", "A sturdy woodcutter's axe.", ICON_IRON_AXE, IF_EQUIP, EQ_WEAPON, 1, -2, 4, 0, 0, 56, TIER_IRON},
    [I_STEEL_AXE] = {"Steel axe", "A very sharp woodcutter's axe.", ICON_STEEL_AXE, IF_EQUIP, EQ_WEAPON, 5, -2, 6, 0, 0, 200, TIER_STEEL},
    [I_BRONZE_PICK] = {"Bronze pickaxe", "Used for mining.", ICON_BRONZE_PICK, IF_EQUIP, EQ_WEAPON, 1, 4, 3, 0, 0, 1, TIER_BRONZE},
    [I_IRON_PICK] = {"Iron pickaxe", "Used for mining. Faster than bronze.", ICON_IRON_PICK, IF_EQUIP, EQ_WEAPON, 1, 5, 5, 0, 0, 140, TIER_IRON},
    [I_NET] = {"Small fishing net", "Useful for catching small fish.", ICON_NET, 0, 0, 0, 0, 0, 0, 0, 5, 0},
    [I_ROD] = {"Fly fishing rod", "Useful for fly fishing.", ICON_ROD, 0, 0, 0, 0, 0, 0, 0, 5, 0},
    [I_FEATHER] = {"Feather", "Used for fly fishing.", ICON_FEATHER, IF_STACK, 0, 0, 0, 0, 0, 0, 2, 0},
    [I_RAW_SHRIMP] = {"Raw shrimps", "I should try cooking this.", ICON_RAW_SHRIMP, 0, 0, 0, 0, 0, 0, 0, 5, 0},
    [I_SHRIMP] = {"Shrimps", "Some nicely cooked shrimp.", ICON_SHRIMP, IF_EAT, 0, 0, 0, 0, 0, 3, 5, 0},
    [I_BURNT_SHRIMP] = {"Burnt shrimp", "Oops!", ICON_BURNT_SHRIMP, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_RAW_ANCHOVY] = {"Raw anchovies", "I should try cooking this.", ICON_RAW_ANCHOVY, 0, 0, 0, 0, 0, 0, 0, 15, 0},
    [I_ANCHOVY] = {"Anchovies", "Some cooked anchovies.", ICON_ANCHOVY, IF_EAT, 0, 0, 0, 0, 0, 1, 15, 0},
    [I_RAW_TROUT] = {"Raw trout", "I should try cooking this.", ICON_RAW_TROUT, 0, 0, 0, 0, 0, 0, 0, 20, 0},
    [I_TROUT] = {"Trout", "Some nicely cooked trout.", ICON_TROUT, IF_EAT, 0, 0, 0, 0, 0, 7, 20, 0},
    [I_RAW_SALMON] = {"Raw salmon", "I should try cooking this.", ICON_RAW_SALMON, 0, 0, 0, 0, 0, 0, 0, 50, 0},
    [I_SALMON] = {"Salmon", "Some nicely cooked salmon.", ICON_SALMON, IF_EAT, 0, 0, 0, 0, 0, 9, 50, 0},
    [I_BURNT_FISH] = {"Burnt fish", "Oops!", ICON_BURNT_FISH, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_RAW_BEEF] = {"Raw beef", "I need to cook this first.", ICON_RAW_BEEF, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_COOKED_MEAT] = {"Cooked meat", "Mmm, this looks tasty.", ICON_COOKED_MEAT, IF_EAT, 0, 0, 0, 0, 0, 3, 4, 0},
    [I_BURNT_MEAT] = {"Burnt meat", "Oh dear, it's totally burnt!", ICON_BURNT_MEAT, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_RAW_CHICKEN] = {"Raw chicken", "I need to cook this first.", ICON_RAW_CHICKEN, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_COOKED_CHICKEN] = {"Cooked chicken", "Mmm, this looks tasty.", ICON_COOKED_CHICKEN, IF_EAT, 0, 0, 0, 0, 0, 3, 4, 0},
    [I_BONES] = {"Bones", "Bones are for burying!", ICON_BONES, IF_BURY, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_COWHIDE] = {"Cowhide", "I should take this to a tanner.", ICON_COWHIDE, 0, 0, 0, 0, 0, 0, 0, 25, 0},
    [I_COPPER_ORE] = {"Copper ore", "This needs refining.", ICON_COPPER_ORE, 0, 0, 0, 0, 0, 0, 0, 3, 0},
    [I_TIN_ORE] = {"Tin ore", "This needs refining.", ICON_TIN_ORE, 0, 0, 0, 0, 0, 0, 0, 3, 0},
    [I_IRON_ORE] = {"Iron ore", "This needs refining.", ICON_IRON_ORE, 0, 0, 0, 0, 0, 0, 0, 17, 0},
    [I_BRONZE_SWORD] = {"Bronze sword", "A razor sharp sword.", ICON_BRONZE_SWORD, IF_EQUIP, EQ_WEAPON, 1, 4, 5, 0, 0, 26, TIER_BRONZE},
    [I_IRON_SWORD] = {"Iron sword", "A razor sharp sword.", ICON_IRON_SWORD, IF_EQUIP, EQ_WEAPON, 1, 6, 7, 0, 0, 91, TIER_IRON},
    [I_STEEL_SWORD] = {"Steel sword", "A razor sharp sword.", ICON_STEEL_SWORD, IF_EQUIP, EQ_WEAPON, 5, 11, 12, 0, 0, 325, TIER_STEEL},
    [I_BRONZE_HELM] = {"Bronze med helm", "A medium sized helmet.", ICON_BRONZE_HELM, IF_EQUIP, EQ_HEAD, 1, 0, 0, 3, 0, 24, TIER_BRONZE},
    [I_IRON_HELM] = {"Iron med helm", "A medium sized helmet.", ICON_IRON_HELM, IF_EQUIP, EQ_HEAD, 1, 0, 0, 4, 0, 84, TIER_IRON},
    [I_STEEL_HELM] = {"Steel med helm", "A medium sized helmet.", ICON_STEEL_HELM, IF_EQUIP, EQ_HEAD, 5, 0, 0, 7, 0, 300, TIER_STEEL},
    [I_WOODEN_SHIELD] = {"Wooden shield", "A solid wooden shield.", ICON_WOODEN_SHIELD, IF_EQUIP, EQ_SHIELD, 1, 0, 0, 4, 0, 20, TIER_WOOD},
    [I_BRONZE_SHIELD] = {"Bronze sq shield", "A medium square shield.", ICON_BRONZE_SHIELD, IF_EQUIP, EQ_SHIELD, 1, 0, 0, 5, 0, 48, TIER_BRONZE},
    [I_IRON_SHIELD] = {"Iron sq shield", "A medium square shield.", ICON_IRON_SHIELD, IF_EQUIP, EQ_SHIELD, 1, 0, 0, 7, 0, 168, TIER_IRON},
    [I_STEEL_SHIELD] = {"Steel sq shield", "A medium square shield.", ICON_STEEL_SHIELD, IF_EQUIP, EQ_SHIELD, 5, 0, 0, 11, 0, 600, TIER_STEEL},
    [I_LEATHER_BODY] = {"Leather body", "Better than no armour!", ICON_LEATHER_BODY, IF_EQUIP, EQ_BODY, 1, 0, 0, 8, 0, 21, TIER_LEATHER},
    [I_BRONZE_BODY] = {"Bronze platebody", "Provides excellent protection.", ICON_BRONZE_BODY, IF_EQUIP, EQ_BODY, 1, 0, 0, 15, 0, 160, TIER_BRONZE},
    [I_IRON_BODY] = {"Iron platebody", "Provides excellent protection.", ICON_IRON_BODY, IF_EQUIP, EQ_BODY, 1, 0, 0, 21, 0, 560, TIER_IRON},
    [I_LEATHER_CHAPS] = {"Leather chaps", "Better than no armour!", ICON_LEATHER_CHAPS, IF_EQUIP, EQ_LEGS, 1, 0, 0, 3, 0, 20, TIER_LEATHER},
    [I_BRONZE_LEGS] = {"Bronze platelegs", "Big, bronze and heavy looking.", ICON_BRONZE_LEGS, IF_EQUIP, EQ_LEGS, 1, 0, 0, 8, 0, 80, TIER_BRONZE},
    [I_IRON_LEGS] = {"Iron platelegs", "Big, iron and heavy looking.", ICON_IRON_LEGS, IF_EQUIP, EQ_LEGS, 1, 0, 0, 11, 0, 280, TIER_IRON},
    [I_BOOTS] = {"Leather boots", "Comfortable leather boots.", ICON_BOOTS, IF_EQUIP, EQ_FEET, 1, 0, 0, 1, 0, 6, TIER_LEATHER},
    [I_MITHRIL_AXE] = {"Mithril axe", "A powerful axe.", ICON_MITHRIL_AXE, IF_EQUIP, EQ_WEAPON, 20, 12, 13, 0, 0, 520, TIER_MITHRIL},
    [I_ADAMANT_AXE] = {"Adamant axe", "A powerful axe.", ICON_ADAMANT_AXE, IF_EQUIP, EQ_WEAPON, 30, 17, 19, 0, 0, 1280, TIER_ADAMANT},
    [I_RUNE_AXE] = {"Rune axe", "A powerful axe.", ICON_RUNE_AXE, IF_EQUIP, EQ_WEAPON, 40, 26, 29, 0, 0, 12800, TIER_RUNE},
    [I_STEEL_PICK] = {"Steel pickaxe", "Used for mining.", ICON_STEEL_PICK, IF_EQUIP, EQ_WEAPON, 5, 8, 9, 0, 0, 500, TIER_STEEL},
    [I_MITHRIL_PICK] = {"Mithril pickaxe", "Used for mining.", ICON_MITHRIL_PICK, IF_EQUIP, EQ_WEAPON, 20, 12, 13, 0, 0, 1300, TIER_MITHRIL},
    [I_ADAMANT_PICK] = {"Adamant pickaxe", "Used for mining.", ICON_ADAMANT_PICK, IF_EQUIP, EQ_WEAPON, 30, 17, 19, 0, 0, 3200, TIER_ADAMANT},
    [I_RUNE_PICK] = {"Rune pickaxe", "Used for mining.", ICON_RUNE_PICK, IF_EQUIP, EQ_WEAPON, 40, 26, 29, 0, 0, 32000, TIER_RUNE},
    [I_BRONZE_DAGGER] = {"Bronze dagger", "Short but pointy.", ICON_BRONZE_DAGGER, IF_EQUIP, EQ_WEAPON, 1, 4, 3, 0, 0, 10, TIER_BRONZE},
    [I_IRON_MACE] = {"Iron mace", "A spiky mace.", ICON_IRON_MACE, IF_EQUIP, EQ_WEAPON, 1, 8, 7, 0, 0, 63, TIER_IRON},
    [I_IRON_SCIMITAR] = {"Iron scimitar", "A vicious, curved sword.", ICON_IRON_SCIMITAR, IF_EQUIP, EQ_WEAPON, 1, 10, 9, 0, 0, 112, TIER_IRON},
    [I_MITHRIL_SCIMITAR] = {"Mithril scimitar", "A vicious, curved sword.", ICON_MITHRIL_SCIMITAR, IF_EQUIP, EQ_WEAPON, 20, 21, 20, 0, 0, 1040, TIER_MITHRIL},
    [I_ADAMANT_SCIMITAR] = {"Adamant scimitar", "A vicious, curved sword.", ICON_ADAMANT_SCIMITAR, IF_EQUIP, EQ_WEAPON, 30, 29, 28, 0, 0, 2560, TIER_ADAMANT},
    [I_RUNE_SCIMITAR] = {"Rune scimitar", "A vicious, curved sword.", ICON_RUNE_SCIMITAR, IF_EQUIP, EQ_WEAPON, 40, 45, 44, 0, 0, 25600, TIER_RUNE},
    [I_STEEL_LONGSWORD] = {"Steel longsword", "A razor sharp longsword.", ICON_STEEL_LONGSWORD, IF_EQUIP, EQ_WEAPON, 5, 17, 16, 0, 0, 600, TIER_STEEL},
    [I_RUNE_LONGSWORD] = {"Rune longsword", "A razor sharp longsword.", ICON_RUNE_LONGSWORD, IF_EQUIP, EQ_WEAPON, 40, 47, 49, 0, 0, 32000, TIER_RUNE},
    [I_BRONZE_FULL_HELM] = {"Bronze full helm", "A full face helmet.", ICON_BRONZE_FULL_HELM, IF_EQUIP, EQ_HEAD, 1, 0, 0, 4, 0, 44, TIER_BRONZE},
    [I_IRON_FULL_HELM] = {"Iron full helm", "A full face helmet.", ICON_IRON_FULL_HELM, IF_EQUIP, EQ_HEAD, 1, 0, 0, 6, 0, 154, TIER_IRON},
    [I_MITHRIL_FULL_HELM] = {"Mithril full helm", "A full face helmet.", ICON_MITHRIL_FULL_HELM, IF_EQUIP, EQ_HEAD, 20, 0, 0, 13, 0, 1430, TIER_MITHRIL},
    [I_ADAMANT_FULL_HELM] = {"Adamant full helm", "A full face helmet.", ICON_ADAMANT_FULL_HELM, IF_EQUIP, EQ_HEAD, 30, 0, 0, 19, 0, 3520, TIER_ADAMANT},
    [I_RUNE_FULL_HELM] = {"Rune full helm", "A full face helmet.", ICON_RUNE_FULL_HELM, IF_EQUIP, EQ_HEAD, 40, 0, 0, 31, 0, 35200, TIER_RUNE},
    [I_BRONZE_CHAINBODY] = {"Bronze chainbody", "A series of connected metal rings.", ICON_BRONZE_CHAINBODY, IF_EQUIP, EQ_BODY, 1, 0, 0, 11, 0, 60, TIER_BRONZE},
    [I_IRON_CHAINBODY] = {"Iron chainbody", "A series of connected metal rings.", ICON_IRON_CHAINBODY, IF_EQUIP, EQ_BODY, 1, 0, 0, 16, 0, 210, TIER_IRON},
    [I_STEEL_BODY] = {"Steel platebody", "Provides excellent protection.", ICON_STEEL_BODY, IF_EQUIP, EQ_BODY, 5, 0, 0, 32, 0, 2000, TIER_STEEL},
    [I_MITHRIL_BODY] = {"Mithril platebody", "Provides excellent protection.", ICON_MITHRIL_BODY, IF_EQUIP, EQ_BODY, 20, 0, 0, 46, 0, 5200, TIER_MITHRIL},
    [I_ADAMANT_BODY] = {"Adamant platebody", "Provides excellent protection.", ICON_ADAMANT_BODY, IF_EQUIP, EQ_BODY, 30, 0, 0, 65, 0, 16640, TIER_ADAMANT},
    [I_RUNE_BODY] = {"Rune platebody", "Provides excellent protection.", ICON_RUNE_BODY, IF_EQUIP, EQ_BODY, 40, 0, 0, 82, 0, 65000, TIER_RUNE},
    [I_BRONZE_SKIRT] = {"Bronze plateskirt", "Designer leg protection.", ICON_BRONZE_SKIRT, IF_EQUIP, EQ_LEGS, 1, 0, 0, 8, 0, 80, TIER_BRONZE},
    [I_IRON_SKIRT] = {"Iron plateskirt", "Designer leg protection.", ICON_IRON_SKIRT, IF_EQUIP, EQ_LEGS, 1, 0, 0, 11, 0, 280, TIER_IRON},
    [I_STEEL_LEGS] = {"Steel platelegs", "These look pretty heavy.", ICON_STEEL_LEGS, IF_EQUIP, EQ_LEGS, 5, 0, 0, 17, 0, 1000, TIER_STEEL},
    [I_MITHRIL_LEGS] = {"Mithril platelegs", "These look pretty heavy.", ICON_MITHRIL_LEGS, IF_EQUIP, EQ_LEGS, 20, 0, 0, 24, 0, 2600, TIER_MITHRIL},
    [I_ADAMANT_LEGS] = {"Adamant platelegs", "These look pretty heavy.", ICON_ADAMANT_LEGS, IF_EQUIP, EQ_LEGS, 30, 0, 0, 33, 0, 6400, TIER_ADAMANT},
    [I_RUNE_LEGS] = {"Rune platelegs", "These look pretty heavy.", ICON_RUNE_LEGS, IF_EQUIP, EQ_LEGS, 40, 0, 0, 51, 0, 64000, TIER_RUNE},
    [I_IRON_KITE] = {"Iron kiteshield", "A large metal shield.", ICON_IRON_KITE, IF_EQUIP, EQ_SHIELD, 1, 0, 0, 10, 0, 238, TIER_IRON},
    [I_MITHRIL_KITE] = {"Mithril kiteshield", "A large metal shield.", ICON_MITHRIL_KITE, IF_EQUIP, EQ_SHIELD, 20, 0, 0, 22, 0, 2210, TIER_MITHRIL},
    [I_ADAMANT_KITE] = {"Adamant kiteshield", "A large metal shield.", ICON_ADAMANT_KITE, IF_EQUIP, EQ_SHIELD, 30, 0, 0, 31, 0, 5440, TIER_ADAMANT},
    [I_RUNE_KITE] = {"Rune kiteshield", "A large metal shield.", ICON_RUNE_KITE, IF_EQUIP, EQ_SHIELD, 40, 0, 0, 46, 0, 54400, TIER_RUNE},
    [I_IRON_BOOTS] = {"Iron boots", "These will protect my feet.", ICON_IRON_BOOTS, IF_EQUIP, EQ_FEET, 1, 0, 0, 3, 0, 56, TIER_IRON},
    [I_YEW_LOGS] = {"Yew logs", "Logs cut from a yew tree.", ICON_WILLOW_LOGS, IF_LIGHT, 0, 0, 0, 0, 0, 0, 160, 0},
    [I_EGG] = {"Egg", "A nice fresh egg.", ICON_EGG, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_BUCKET] = {"Bucket", "It's an empty bucket.", ICON_BUCKET, 0, 0, 0, 0, 0, 0, 0, 2, 0},
    [I_BUCKET_MILK] = {"Bucket of milk", "It's a bucket of milk.", ICON_BUCKET_MILK, 0, 0, 0, 0, 0, 0, 0, 12, 0},
    [I_BUCKET_WATER] = {"Bucket of water", "It's a bucket of water.", ICON_BUCKET_WATER, 0, 0, 0, 0, 0, 0, 0, 6, 0},
    [I_POT] = {"Pot", "This pot is empty.", ICON_POT, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_POT_FLOUR] = {"Pot of flour", "There is flour in this pot.", ICON_POT_FLOUR, 0, 0, 0, 0, 0, 0, 0, 14, 0},
    [I_GRAIN] = {"Grain", "Some wheat heads.", ICON_GRAIN, 0, 0, 0, 0, 0, 0, 0, 2, 0},
    [I_SHEARS] = {"Shears", "For shearing sheep.", ICON_SHEARS, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_WOOL] = {"Wool", "I think this came from a sheep.", ICON_WOOL, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_BALL_WOOL] = {"Ball of wool", "Spun from sheep's wool.", ICON_BALL_WOOL, 0, 0, 0, 0, 0, 0, 0, 2, 0},
    [I_GHOSTSPEAK] = {"Ghostspeak amulet", "It lets me talk to ghosts.", ICON_GHOSTSPEAK, IF_EQUIP, EQ_NECK, 1, 0, 0, 0, 0, 35, 0},
    [I_SKULL] = {"Ghost's skull", "Ooh! A nasty looking skull.", ICON_SKULL, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_MESSAGE] = {"Message", "A message from Juliet to Romeo.", ICON_SCROLL, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_CADAVA] = {"Cadava berries", "Poisonous berries.", ICON_CADAVA, 0, 0, 0, 0, 0, 0, 0, 6, 0},
    [I_CADAVA_POTION] = {"Cadava potion", "This potion makes you appear dead.", ICON_CADAVA_POTION, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_CLAY] = {"Clay", "Some hard dry clay.", ICON_CLAY, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_AIR_TALISMAN] = {"Air talisman", "A mysterious power emanates from it.", ICON_TALISMAN, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_PACKAGE] = {"Research package", "Important research for Aubury.", ICON_PACKAGE, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_NOTES] = {"Research notes", "Aubury's notes on the talisman.", ICON_SCROLL, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_RED_BEAD] = {"Red bead", "A small round red bead.", ICON_RED_BEAD, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_YELLOW_BEAD] = {"Yellow bead", "A small round yellow bead.", ICON_YELLOW_BEAD, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_BLACK_BEAD] = {"Black bead", "A small round black bead.", ICON_BLACK_BEAD, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_WHITE_BEAD] = {"White bead", "A small round white bead.", ICON_WHITE_BEAD, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_ACCURACY] = {"Amulet of accuracy", "It increases my aim.", ICON_ACCURACY, IF_EQUIP, EQ_NECK, 1, 4, 0, 0, 0, 100, 0},
    [I_EYE_NEWT] = {"Eye of newt", "It seems to be looking at me.", ICON_EYE_NEWT, 0, 0, 0, 0, 0, 0, 0, 3, 0},
    [I_ONION] = {"Onion", "A strong smelling onion.", ICON_ONION, 0, 0, 0, 0, 0, 0, 0, 3, 0},
    [I_RAT_TAIL] = {"Rat's tail", "A bit of rat.", ICON_RAT_TAIL, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_GOBLIN_MAIL] = {"Goblin mail", "Armour designed to fit goblins.", ICON_GOBLIN_MAIL, 0, 0, 0, 0, 0, 0, 0, 40, 0},
    [I_ORANGE_DYE] = {"Orange dye", "A little bottle of orange dye.", ICON_ORANGE_DYE, 0, 0, 0, 0, 0, 0, 0, 6, 0},
    [I_BLUE_DYE] = {"Blue dye", "A little bottle of blue dye.", ICON_BLUE_DYE, 0, 0, 0, 0, 0, 0, 0, 6, 0},
    [I_ORANGE_MAIL] = {"Orange goblin mail", "Goblin armour, dyed orange.", ICON_ORANGE_MAIL, 0, 0, 0, 0, 0, 0, 0, 40, 0},
    [I_BLUE_MAIL] = {"Blue goblin mail", "Goblin armour, dyed blue.", ICON_BLUE_MAIL, 0, 0, 0, 0, 0, 0, 0, 40, 0},
    [I_GOLD_BAR] = {"Gold bar", "It's a bar of gold.", ICON_GOLD_BAR, 0, 0, 0, 0, 0, 0, 0, 300, 0},
    [I_FISH_FOOD] = {"Fish food", "Keeps your pet fish strong and healthy.", ICON_FISH_FOOD, 0, 0, 0, 0, 0, 0, 0, 10, 0},
    [I_POISON] = {"Poison", "This stuff looks nasty.", ICON_POISON, 0, 0, 0, 0, 0, 0, 0, 5, 0},
    [I_POISON_FOOD] = {"Poisoned fish food", "Fish food laced with poison.", ICON_POISON_FOOD, 0, 0, 0, 0, 0, 0, 0, 10, 0},
    [I_GAUGE] = {"Pressure gauge", "It looks like part of a machine.", ICON_GAUGE, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_TUBE] = {"Rubber tube", "It's slightly charred.", ICON_TUBE, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_OIL_CAN] = {"Oil can", "It's an oil can.", ICON_OIL_CAN, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_MANOR_KEY] = {"Key", "A key I found in a compost heap.", ICON_KEY, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_SPADE] = {"Spade", "A slightly muddy spade.", ICON_SPADE, 0, 0, 0, 0, 0, 0, 0, 3, 0},
    [I_RUM] = {"Karamja rum", "A very strong spirit brewed in Karamja.", ICON_RUM, 0, 0, 0, 0, 0, 0, 0, 30, 0},
    [I_CHEST_KEY] = {"Chest key", "Opens a chest in the Blue Moon Inn.", ICON_GOLD_KEY, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_PIRATE_MSG] = {"Pirate message", "A torn note with directions.", ICON_PIRATE_MSG, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_GOLD_RING] = {"Gold ring", "A valuable ring.", ICON_GOLD_RING, 0, 0, 0, 0, 0, 0, 0, 350, 0},
    [I_EMERALD] = {"Emerald", "This looks valuable.", ICON_EMERALD, 0, 0, 0, 0, 0, 0, 0, 1000, 0},
    [I_CABBAGE] = {"Cabbage", "Yuck, I don't like cabbage.", ICON_CABBAGE, IF_EAT, 0, 0, 0, 0, 0, 1, 1, 0},
    [I_SILVERLIGHT] = {"Silverlight", "The magical sword Silverlight.", ICON_SILVERLIGHT, IF_EQUIP, EQ_WEAPON, 1, 10, 12, 0, 0, 50, TIER_STEEL},
    [I_KEY_ROVIN] = {"Silverlight key", "A key given to me by Captain Rovin.", ICON_KEY, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_KEY_TRAIBORN] = {"Silverlight key", "A key given to me by Wizard Traiborn.", ICON_KEY, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_KEY_DRAIN] = {"Silverlight key", "A key I found in the palace drain.", ICON_KEY, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_JUG] = {"Jug", "An empty jug.", ICON_JUG, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_KNIFE] = {"Knife", "A dangerous looking knife.", ICON_KNIFE, 0, 0, 0, 0, 0, 0, 0, 6, 0},
    [I_BOWL] = {"Bowl", "Useful for mixing things.", ICON_BOWL, 0, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_CAKE_TIN] = {"Cake tin", "Useful for baking cakes.", ICON_CAKE_TIN, 0, 0, 0, 0, 0, 0, 0, 10, 0},
    [I_CHISEL] = {"Chisel", "Good for detailed crafting.", ICON_CHISEL, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_HAMMER] = {"Hammer", "Good for hitting things.", ICON_HAMMER, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_NEWCOMER_MAP] = {"Newcomer map", "Issued to all new citizens of Gielinor.", ICON_NEWCOMER_MAP, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    [I_SECURITY_BOOK] = {"Security book", "An old book about keeping your account safe.", ICON_SECURITY_BOOK, 0, 0, 0, 0, 0, 0, 0, 2, 0},
    // bows (req: Ranged level) and arrows; the last three numbers are ranged attack / strength, magic
    [I_SHORTBOW] = {"Shortbow", "Short and effective.", ICON_SHORTBOW, BOW(1, 50, 8)},
    [I_LONGBOW] = {"Longbow", "A nice sturdy bow.", ICON_LONGBOW, BOW(1, 80, 8)},
    [I_OAK_SHORTBOW] = {"Oak shortbow", "A shortbow made out of oak, still effective.", ICON_OAK_SHORTBOW, BOW(5, 100, 14)},
    [I_OAK_LONGBOW] = {"Oak longbow", "A nice sturdy bow made out of oak.", ICON_OAK_LONGBOW, BOW(5, 160, 14)},
    [I_WILLOW_SHORTBOW] = {"Willow shortbow", "A shortbow made out of willow, still effective.", ICON_WILLOW_SHORTBOW, BOW(20, 200, 20)},
    [I_WILLOW_LONGBOW] = {"Willow longbow", "A nice sturdy bow made out of willow.", ICON_WILLOW_LONGBOW, BOW(20, 320, 20)},
    [I_MAPLE_SHORTBOW] = {"Maple shortbow", "A shortbow made out of maple, still effective.", ICON_MAPLE_SHORTBOW, BOW(30, 400, 29)},
    [I_MAPLE_LONGBOW] = {"Maple longbow", "A nice sturdy bow made out of maple.", ICON_MAPLE_LONGBOW, BOW(30, 640, 29)},
    [I_BRONZE_ARROW] = {"Bronze arrow", "Arrows with bronze heads.", ICON_BRONZE_ARROW, ARROW(1, 1, 7, TIER_BRONZE)},
    [I_IRON_ARROW] = {"Iron arrow", "Arrows with iron heads.", ICON_IRON_ARROW, ARROW(1, 3, 10, TIER_IRON)},
    [I_STEEL_ARROW] = {"Steel arrow", "Arrows with steel heads.", ICON_STEEL_ARROW, ARROW(5, 12, 16, TIER_STEEL)},
    [I_MITHRIL_ARROW] = {"Mithril arrow", "Arrows with mithril heads.", ICON_MITHRIL_ARROW, ARROW(20, 32, 22, TIER_MITHRIL)},
    [I_ADAMANT_ARROW] = {"Adamant arrow", "Arrows with adamant heads.", ICON_ADAMANT_ARROW, ARROW(30, 80, 31, TIER_ADAMANT)},
    // runes and staves
    [I_AIR_RUNE] = {"Air rune", "One of the 4 basic elemental Runes.", ICON_AIR_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_WATER_RUNE] = {"Water rune", "One of the 4 basic elemental Runes.", ICON_WATER_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_EARTH_RUNE] = {"Earth rune", "One of the 4 basic elemental Runes.", ICON_EARTH_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_FIRE_RUNE] = {"Fire rune", "One of the 4 basic elemental Runes.", ICON_FIRE_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 4, 0},
    [I_MIND_RUNE] = {"Mind rune", "Used for basic level missile spells.", ICON_MIND_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 3, 0},
    [I_BODY_RUNE] = {"Body rune", "Used for curse spells.", ICON_BODY_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 3, 0},
    [I_CHAOS_RUNE] = {"Chaos rune", "Used for low level missile spells.", ICON_CHAOS_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 90, 0},
    [I_DEATH_RUNE] = {"Death rune", "Used for medium level missile spells.", ICON_DEATH_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 180, 0},
    [I_STAFF] = {"Staff", "It's a slightly magical stick.", ICON_STAFF, STAFF(7, 7, 15, 4)},
    [I_MAGIC_STAFF] = {"Magic staff", "A Magical staff.", ICON_MAGIC_STAFF, STAFF(7, 7, 200, 10)},
    [I_STAFF_AIR] = {"Staff of air", "A Magical staff.", ICON_STAFF_AIR, STAFF(10, 7, 1500, 10)},
    [I_STAFF_WATER] = {"Staff of water", "A Magical staff.", ICON_STAFF_WATER, STAFF(10, 7, 1500, 10)},
    [I_STAFF_EARTH] = {"Staff of earth", "A Magical staff.", ICON_STAFF_EARTH, STAFF(10, 7, 1500, 10)},
    [I_STAFF_FIRE] = {"Staff of fire", "A Magical staff.", ICON_STAFF_FIRE, STAFF(10, 7, 1500, 10)},
    [I_COSMIC_RUNE] = {"Cosmic rune", "Used for enchant spells.", ICON_COSMIC_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 50, 0},
    [I_LAW_RUNE] = {"Law rune", "Used for teleport spells.", ICON_LAW_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 240, 0},
    [I_NATURE_RUNE] = {"Nature rune", "Used for alchemy spells.", ICON_NATURE_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 180, 0},
    [I_BLOOD_RUNE] = {"Blood rune", "Used for high level missile spells.", ICON_BLOOD_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 400, 0},
    [I_SOUL_RUNE] = {"Soul rune", "Used for high level curse spells.", ICON_SOUL_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 300, 0},
    [I_WRATH_RUNE] = {"Wrath rune", "Used for the most powerful missile spells.", ICON_WRATH_RUNE, IF_STACK, 0, 0, 0, 0, 0, 0, 400, 0},
};

const char *const skill_names[SK_COUNT] = {
    "Attack", "Strength", "Defence", "Hitpoints", "Prayer",
    "Woodcutting", "Firemaking", "Fishing", "Cooking", "Mining", "Ranged", "Magic",
};
const u8 skill_icons[SK_COUNT] = {
    ICON_STEEL_SWORD, ICON_SK_FIST, ICON_STEEL_SHIELD, ICON_SK_HEART, ICON_SK_STAR,
    ICON_IRON_AXE, ICON_SK_FLAME, ICON_RAW_TROUT, ICON_SK_POT, ICON_IRON_PICK, ICON_SK_BOW, ICON_SK_MAGIC,
};

static const char *const dlg_guide[] = {
    "Greetings, adventurer! Welcome to Lumbridge.",
    "Press START at any time to open your menu. From there you can manage your items, equipment and skills.",
    "Face a tree and press A to chop it - you'll need an axe. Rocks need a pickaxe, fishing spots need a net.",
    "Light logs from your Items menu to make a fire, then press A at the fire to cook raw food.",
    "Chickens drop feathers - you'll need those for fly fishing up the river. Hold B to run.",
    "The bank is upstairs... well, just north in the castle. Remember to save from the System menu!",
    "People all over the land need help. The Quests page of your menu lists every quest you can take on.",
    "The Cook in the castle kitchen looks like he could use a hand - why not start there?",
    0};
static const char *const dlg_hans[] = {
    "Hello. What are you doing here?",
    "I've been patrolling this castle for years. Some say I'll be here forever...",
    0};
static const char *const dlg_cook[] = {
    "What am I to do? Oh, hello there.",
    "Feel free to use my range. Food burns less on a range than on an open fire.",
    0};
static const char *const dlg_shop[] = {"Can I help you at all?", 0};
static const char *const dlg_bank[] = {"Good day. How may I help you?", 0};
static const char *const dlg_bob[] = {"Hello. I'm Bob. I sell axes, and a pickaxe or two for the miners.", 0};
static const char *const dlg_gerrant[] = {"Welcome! You can buy fishing equipment at my store. We'll also buy fish that you catch off you.", 0};
static const char *const dlg_betty[] = {"Welcome to the Magic Emporium. Eye of newt for your potions?", 0};
static const char *const dlg_swords[] = {"Hello, adventurer. Can I interest you in some swords?", 0};
static const char *const dlg_horvik[] = {"Hello, do you need any help? I've the finest armour in Varrock.", 0};
static const char *const dlg_peksa[] = {"Are you interested in buying or selling a helmet?", 0};
static const char *const dlg_cassie[] = {"I buy and sell shields. Do you want to trade?", 0};
static const char *const dlg_wayne[] = {"Welcome to Wayne's Chains. Do you wanna buy or sell some chain mail?", 0};
static const char *const dlg_zeke[] = {"A thousand greetings. Would you like to buy a superior scimitar?", 0};
static const char *const dlg_louie[] = {"Hey, wanna buy some armour? I've got the best legs in the desert.", 0};
static const char *const dlg_nurmof[] = {"Greetings and well met! I sell the finest pickaxes in all the land.", 0};
static const char *const dlg_lowe[] = {"Welcome to Lowe's Archery Emporium. Do you want to see my wares?", 0};
static const char *const dlg_zaff[] = {"Would you like to buy or sell some staffs?", 0};

const NpcDef npc_defs[NPC_KIND_COUNT] = {
    [NPC_COW] = {"Cow", SPR_KIND_COW, 5, 2, 8, 1, 1, 1, 0, 0, -21, NF_ATTACK, 4, 40, 0},
    [NPC_CHICKEN] = {"Chicken", SPR_KIND_CHICKEN, 5, 1, 3, 1, 1, 1, 0, 0, -42, NF_ATTACK, 4, 30, 0},
    [NPC_GOBLIN] = {"Goblin", SPR_KIND_HUMAN, 3, 2, 5, 1, 1, 1, 0, 0, -15, NF_ATTACK | NF_AGGRO, 4, 40, 0},
    [NPC_MAN] = {"Man", SPR_KIND_HUMAN, 1, 2, 7, 1, 1, 1, 0, 0, -21, NF_ATTACK, 3, 50, 0},
    [NPC_WOMAN] = {"Woman", SPR_KIND_HUMAN, 7, 2, 7, 1, 1, 1, 0, 0, -21, NF_ATTACK, 3, 50, 0},
    [NPC_GUARD] = {"Guard", SPR_KIND_HUMAN, 2, 21, 22, 19, 18, 14, 10, 8, 20, NF_ATTACK, 3, 80, 0},
    [NPC_BANKER] = {"Banker", SPR_KIND_HUMAN, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_BANK, 0, 0, dlg_bank},
    [NPC_SHOPKEEPER] = {"Shopkeeper", SPR_KIND_HUMAN, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_shop},
    [NPC_GUIDE] = {"Lumbridge Guide", SPR_KIND_HUMAN, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, dlg_guide},
    [NPC_COOK] = {"Cook", SPR_KIND_HUMAN, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, dlg_cook},
    [NPC_HANS] = {"Hans", SPR_KIND_HUMAN, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 4, 0, dlg_hans},
    [NPC_WARRIOR] = {"Al Kharid warrior", SPR_KIND_HUMAN, 2, 9, 19, 7, 5, 4, 5, 5, 3, NF_ATTACK, 3, 50, 0},
    [NPC_DARK_WIZARD] = {"Dark wizard", SPR_KIND_HUMAN, 4, 7, 12, 6, 6, 4, 4, 2, 0, NF_ATTACK | NF_AGGRO, 2, 50, 0},
    [NPC_BARBARIAN] = {"Barbarian", SPR_KIND_HUMAN, 1, 10, 14, 6, 7, 4, 4, 4, 0, NF_ATTACK, 3, 50, 0},
    [NPC_DUKE] = {"Duke Horacio", SPR_KIND_HUMAN, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_FRED] = {"Fred the Farmer", SPR_KIND_HUMAN, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_SHEEP] = {"Sheep", SPR_KIND_SHEEP, 5, 0, 1, 1, 1, 1, 0, 0, 0, 0, 3, 0, 0},
    [NPC_DAIRY_COW] = {"Dairy cow", SPR_KIND_COW, 5, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
    [NPC_AERECK] = {"Father Aereck", SPR_KIND_PRIEST, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_URHNEY] = {"Father Urhney", SPR_KIND_PRIEST, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_GHOST] = {"Restless ghost", SPR_KIND_GHOST, 5, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_ROMEO] = {"Romeo", SPR_KIND_HUMAN, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_JULIET] = {"Juliet", SPR_KIND_DRESS, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_LAWRENCE] = {"Father Lawrence", SPR_KIND_PRIEST, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_APOTHECARY] = {"Apothecary", SPR_KIND_DRESS, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_DORIC] = {"Doric", SPR_KIND_HUMAN, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_SEDRIDOR] = {"Sedridor", SPR_KIND_WIZARD, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_AUBURY] = {"Aubury", SPR_KIND_WIZARD, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_MIZGOG] = {"Wizard Mizgog", SPR_KIND_WIZARD, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_TRAIBORN] = {"Wizard Traiborn", SPR_KIND_WIZARD, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_HETTY] = {"Hetty", SPR_KIND_WITCH, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_AGGIE] = {"Aggie", SPR_KIND_WITCH, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_WARTFACE] = {"General Wartface", SPR_KIND_HUMAN, 3, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_BENTNOZE] = {"General Bentnoze", SPR_KIND_HUMAN, 3, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_VERONICA] = {"Veronica", SPR_KIND_DRESS, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_ODDENSTEIN] = {"Professor Oddenstein", SPR_KIND_HUMAN, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_FRANK] = {"Redbeard Frank", SPR_KIND_HUMAN, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_BARTENDER] = {"Bartender", SPR_KIND_HUMAN, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_AMIK] = {"Sir Amik Varze", SPR_KIND_HUMAN, 2, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_BK_GUARD] = {"Fortress guard", SPR_KIND_HUMAN, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_BORDER_GUARD] = {"Border guard", SPR_KIND_HUMAN, 2, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_BOB] = {"Bob", SPR_KIND_HUMAN, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_bob},
    [NPC_GERRANT] = {"Gerrant", SPR_KIND_HUMAN, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_gerrant},
    [NPC_BETTY] = {"Betty", SPR_KIND_DRESS, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_betty},
    [NPC_SWORD_KEEPER] = {"Shop keeper", SPR_KIND_HUMAN, 2, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_swords},
    [NPC_HORVIK] = {"Horvik", SPR_KIND_HUMAN, 2, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_horvik},
    [NPC_PEKSA] = {"Peksa", SPR_KIND_HUMAN, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_peksa},
    [NPC_CASSIE] = {"Cassie", SPR_KIND_DRESS, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_cassie},
    [NPC_WAYNE] = {"Wayne", SPR_KIND_HUMAN, 2, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_wayne},
    [NPC_ZEKE] = {"Zeke", SPR_KIND_HUMAN, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_zeke},
    [NPC_LOUIE] = {"Louie Legs", SPR_KIND_HUMAN, 1, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_louie},
    [NPC_NURMOF] = {"Nurmof", SPR_KIND_HUMAN, 3, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_nurmof},
    [NPC_LOWE] = {"Lowe", SPR_KIND_HUMAN, 7, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_lowe},
    [NPC_ZAFF] = {"Zaff", SPR_KIND_WIZARD, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK | NF_SHOP, 0, 0, dlg_zaff},
    [NPC_ARIS] = {"Gypsy Aris", SPR_KIND_DRESS, 4, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_PRYSIN] = {"Sir Prysin", SPR_KIND_HUMAN, 2, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_ROVIN] = {"Captain Rovin", SPR_KIND_HUMAN, 2, 0, 1, 1, 1, 1, 0, 0, 0, NF_TALK, 0, 0, 0},
    [NPC_SKELETON] = {"Skeleton", SPR_KIND_SKELETON, 5, 13, 18, 10, 10, 10, 2, 2, 2, NF_ATTACK | NF_AGGRO, 3, 0, 0},
    [NPC_IMP] = {"Imp", SPR_KIND_IMP, 5, 2, 8, 1, 2, 2, 0, 0, -10, NF_ATTACK, 5, 40, 0},
    [NPC_RAT] = {"Rat", SPR_KIND_RAT, 5, 1, 2, 1, 1, 1, 0, 0, -40, NF_ATTACK, 4, 25, 0},
    [NPC_DELRITH] = {"Delrith", SPR_KIND_DEMON, 5, 27, 35, 18, 16, 18, 10, 6, 10, NF_ATTACK | NF_AGGRO, 2, 0, 0},
};

// OSRS examine text (the target menu's Examine)
static const char *const npc_exam[NPC_KIND_COUNT] = {
    [NPC_COW] = "Converts grass to beef.", [NPC_CHICKEN] = "Yep, definitely a chicken.",
    [NPC_GOBLIN] = "An ugly green creature.", [NPC_MAN] = "One of many citizens.",
    [NPC_WOMAN] = "One of many citizens.", [NPC_GUARD] = "He tries to keep order around here.",
    [NPC_BANKER] = "Good with money.", [NPC_SHOPKEEPER] = "Maybe he'd like to buy some of my junk.",
    [NPC_GUIDE] = "He provides new players with useful information.", [NPC_COOK] = "The head cook of Lumbridge Castle.",
    [NPC_HANS] = "Servant of the Duke of Lumbridge.", [NPC_WARRIOR] = "A warrior of Al Kharid.",
    [NPC_DARK_WIZARD] = "He works evil magic.", [NPC_BARBARIAN] = "Not civilised looking.",
    [NPC_DUKE] = "Duke Horacio of Lumbridge.", [NPC_SHEEP] = "White and fluffy.", [NPC_DAIRY_COW] = "Fat and juicy.",
    [NPC_GHOST] = "Eeek! A ghost!", [NPC_SKELETON] = "Could do with gaining a few pounds.",
    [NPC_IMP] = "A cheeky little imp.", [NPC_RAT] = "Overgrown vermin.", [NPC_DELRITH] = "A demon of immense power.",
    [NPC_AUBURY] = "I think he might be a shop keeper.", [NPC_BETTY] = "A busy shop owner.",
    [NPC_LOWE] = "The owner of the archery store.", [NPC_ZAFF] = "The owner of the staff shop.",
    [NPC_BOB] = "An axe seller.", [NPC_HORVIK] = "An armour seller.", [NPC_BORDER_GUARD] = "Guards the toll gate.",
};
const char *npc_examine(int kind) { return npc_exam[kind]; }

// ---------------------------------------------------------------------------------------
static u32 rng_state = 0x2007BEEF;
void seed_rng(u32 s) { rng_state = s ? s : 1; }
u32 rnd(void) {
    u32 x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng_state = x;
}
int rnd_range(int n) { return n <= 1 ? 0 : (int)(((rnd() >> 16) * (u32)n) >> 16); }   // no division: hot in NPC AI

char *fmt_num(char *buf, u32 v) {
    char tmp[16];
    int n = 0, g = 0;
    do {
        if (g == 3) {
            tmp[n++] = ',';
            g = 0;
        }
        tmp[n++] = '0' + v % 10;
        v /= 10;
        g++;
    } while (v);
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return buf;
}

char *fmt_int(char *buf, s32 v) {
    char tmp[12];
    int n = 0, neg = v < 0;
    u32 u = neg ? (u32)-v : (u32)v;
    do {
        tmp[n++] = '0' + u % 10;
        u /= 10;
    } while (u);
    int o = 0;
    if (neg) buf[o++] = '-';
    while (n) buf[o++] = tmp[--n];
    buf[o] = 0;
    return buf;
}

// ---------------------------------------------------------------------------------------
int level_for_xp(u32 tenths) {
    u32 xp = tenths / 10;
    int l = 1;
    while (l < 99 && xp >= xp_table[l + 1]) l++;
    return l;
}
int skill_level(int sk) { return level_for_xp(pl.xp[sk]); }

// OSRS: a quarter of (Defence + Hitpoints + Prayer / 2), plus the best of melee, ranged and magic
int combat_level(void) {
    int base = 250 * (skill_level(SK_DEFENCE) + skill_level(SK_HITPOINTS) + skill_level(SK_PRAYER) / 2);
    int best = 325 * (skill_level(SK_ATTACK) + skill_level(SK_STRENGTH));
    int range = 325 * (skill_level(SK_RANGED) * 3 / 2), mage = 325 * (skill_level(SK_MAGIC) * 3 / 2);
    if (range > best) best = range;
    if (mage > best) best = mage;
    return (base + best) / 1000;
}

int total_level(void) {
    int t = 0;
    for (int i = 0; i < SK_COUNT; i++) t += skill_level(i);
    return t;
}

void add_xp(int sk, u32 tenths) {
    int before = skill_level(sk);
    pl.xp[sk] += tenths;
    if (pl.xp[sk] > 2000000000u) pl.xp[sk] = 2000000000u;
    xp_drop(sk, tenths);
    int after = skill_level(sk);
    if (after > before) {
        if (sk == SK_HITPOINTS) pl.hp += after - before;
        level_up(sk, after);
    }
}

// ---------------------------------------------------------------------------------------
int inv_count(int id) {
    int n = 0;
    for (int i = 0; i < 28; i++)
        if (pl.inv[i].id == id) n += pl.inv[i].qty;
    return n;
}

int inv_free(void) {
    int n = 0;
    for (int i = 0; i < 28; i++)
        if (!pl.inv[i].id) n++;
    return n;
}

int inv_add(int id, int qty) {
    if (items[id].flags & IF_STACK) {
        for (int i = 0; i < 28; i++)
            if (pl.inv[i].id == id) {
                pl.inv[i].qty += qty;
                return qty;
            }
        for (int i = 0; i < 28; i++)
            if (!pl.inv[i].id) {
                pl.inv[i].id = id;
                pl.inv[i].qty = qty;
                return qty;
            }
        return 0;
    }
    int added = 0;
    for (int i = 0; i < 28 && added < qty; i++)
        if (!pl.inv[i].id) {
            pl.inv[i].id = id;
            pl.inv[i].qty = 1;
            added++;
        }
    return added;
}

int inv_remove(int id, int qty) {
    int removed = 0;
    for (int i = 27; i >= 0 && removed < qty; i--) {
        if (pl.inv[i].id != id) continue;
        int take = pl.inv[i].qty;
        if (take > qty - removed) take = qty - removed;
        pl.inv[i].qty -= take;
        removed += take;
        if (!pl.inv[i].qty) pl.inv[i].id = 0;
    }
    return removed;
}

int has_item(int id) {
    if (inv_count(id)) return 1;
    for (int i = 0; i < EQ_COUNT; i++)
        if (pl.equip[i] == id) return 1;
    return 0;
}

int equip_bonus(int which) {
    int b = 0;
    for (int i = 0; i < EQ_COUNT; i++) {
        const ItemDef *d = &items[pl.equip[i]];
        switch (which) {
        case 0: b += d->atk; break;
        case 1: b += d->str; break;
        case 2: b += d->def; break;
        case 3: b += d->ratk; break;
        case 4: b += d->rstr; break;
        default: b += d->matk; break;
        }
    }
    return b;
}

// wear the item in an inventory slot (arrows go to the ammo slot as a whole stack)
int equip_item(int slot) {
    Slot *s = &pl.inv[slot];
    int id = s->id;
    const ItemDef *d = &items[id];
    if (!(d->flags & IF_EQUIP)) return 0;
    int sk = (d->flags & IF_RANGED) ? SK_RANGED : d->slot == EQ_WEAPON ? SK_ATTACK : SK_DEFENCE;
    if (skill_level(sk) < d->req) {
        char buf[48], *p = buf;
        const char *a = "You need a ", *n = skill_names[sk];
        while (*a) *p++ = *a++;
        while (*n) *p++ = *n++;
        a = " level of ";
        while (*a) *p++ = *a++;
        fmt_int(p, d->req);
        while (*p) p++;
        *p++ = '.';
        *p = 0;
        msg_col(buf, TC_RED);
        return 0;
    }
    if (d->slot == EQ_AMMO) {
        int old = pl.equip[EQ_AMMO], oq = pl.ammo_qty;
        if (old == id) {
            pl.ammo_qty += s->qty;
            s->id = s->qty = 0;
        } else {
            pl.equip[EQ_AMMO] = id;
            pl.ammo_qty = s->qty;
            s->id = old;
            s->qty = old ? oq : 0;
        }
        return 1;
    }
    int old = pl.equip[d->slot];
    pl.equip[d->slot] = id;
    s->id = old;
    s->qty = old ? 1 : 0;
    return 1;
}

int unequip(int eq) {
    int id = pl.equip[eq];
    if (!id) return 1;
    int q = eq == EQ_AMMO ? pl.ammo_qty : 1;
    if (!inv_count(id) && !inv_free()) return 0;
    if (!(items[id].flags & IF_STACK) && inv_free() < q) return 0;
    inv_add(id, q);
    pl.equip[eq] = 0;
    if (eq == EQ_AMMO) pl.ammo_qty = 0;
    return 1;
}

int bank_add(int id, int qty) {
    for (int i = 0; i < 96; i++)
        if (pl.bank[i].id == id) {
            pl.bank[i].qty += qty;
            return qty;
        }
    for (int i = 0; i < 96; i++)
        if (!pl.bank[i].id) {
            pl.bank[i].id = id;
            pl.bank[i].qty = qty;
            return qty;
        }
    return 0;
}
