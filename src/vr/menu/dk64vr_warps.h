// Places the Testing tab can warp to: a name for the menu and the game's map and exit numbers
// (lib/dk64_decomp/include/enums.h, Maps). Exit 0 is each map's default start unless noted.
#pragma once

#include <cstddef>

namespace dk64vr::warps {

struct Warp {
    const char *name;
    int map;
    int exit;
};

inline constexpr Warp List[] = {
    // hub
    { "DK Isles", 0x22, 0 },
    { "Training Grounds", 0xB0, 0 },
    { "DK's Treehouse", 0xAB, 0 },
    // levels (the exits match upstream's debug warp menu)
    { "Jungle Japes", 0x07, 15 },
    { "Angry Aztec", 0x26, 0 },
    { "Frantic Factory", 0x1A, 0 },
    { "Gloomy Galleon", 0x1E, 0 },
    { "Fungi Forest", 0x30, 27 },
    { "Crystal Caves", 0x48, 0 },
    { "Creepy Castle", 0x57, 0 },
    { "Hideout Helm lobby", 0xAA, 0 },
    { "Hideout Helm", 0x11, 0 },
    // lobbies
    { "Japes lobby", 0xA9, 0 },
    { "Aztec lobby", 0xAD, 0 },
    { "Factory lobby", 0xAF, 0 },
    { "Galleon lobby", 0xAE, 0 },
    { "Fungi lobby", 0xB2, 0 },
    { "Caves lobby", 0xC2, 0 },
    { "Castle lobby", 0xC1, 0 },
    // inside levels
    { "Japes: Baboon Blast", 0x25, 0 },
    { "Aztec: Llama temple (water)", 0x14, 0 },
    { "Galleon: lighthouse", 0x31, 0 },
    { "Fungi: giant mushroom (vines)", 0x40, 0 },
    { "Caves: ice castle", 0x62, 0 },
    { "Castle: tree", 0xA4, 0 },
    // animal buddies, vehicles, races
    { "Rambi arena", 0xBF, 0 },
    { "Enguarde arena", 0xB8, 0 },
    { "Japes minecart", 0x06, 0 },
    { "Fungi minecart", 0x37, 0 },
    { "Castle minecart", 0x6A, 0 },
    { "Aztec beetle race", 0x0E, 0 },
    { "Caves beetle race", 0x52, 0 },
    { "Factory car race", 0x1B, 0 },
    { "Castle car race", 0xB9, 0 },
    { "Galleon seal race", 0x27, 0 },
    // bosses
    { "Boss: Army Dillo (Japes)", 0x08, 0 },
    { "Boss: Dogadon (Aztec)", 0xC5, 0 },
    { "Boss: Mad Jack", 0x9A, 0 },
    { "Boss: Pufftoss", 0x6F, 0 },
    { "Boss: Dogadon (Fungi)", 0x53, 0 },
    { "Boss: Army Dillo (Caves)", 0xC4, 0 },
    { "Boss: King Kut Out", 0xC7, 0 },
    { "K. Rool: DK phase", 0xCB, 0 },
    { "K. Rool: Diddy phase", 0xCC, 0 },
    { "K. Rool: Lanky phase", 0xCD, 0 },
    { "K. Rool: Tiny phase", 0xCE, 0 },
    { "K. Rool: Chunky phase", 0xCF, 0 },
    // bonus barrels and battle arenas (easy versions)
    { "Bonus: Kremling Kosh", 0x73, 0 },
    { "Bonus: Big Bug Bash", 0x94, 0 },
    { "Bonus: Mad Maze Maul", 0x44, 0 },
    { "Bonus: Stealthy Snoop", 0x7F, 0 },
    { "Bonus: Busy Barrel Barrage", 0x4E, 0 },
    { "Bonus: Speedy Swing Sortie", 0x63, 0 },
    { "Bonus: Splish-Splash Salvage", 0x85, 0 },
    { "Bonus: Krazy Kong Klamour", 0x65, 0 },
    { "Bonus: Searchlight Seek", 0x67, 0 },
    { "Bonus: Beaver Bother", 0x68, 0 },
    { "Bonus: Peril Path Panic", 0x91, 0 },
    { "Bonus: Minecart Mayhem", 0x4D, 0 },
    { "Bonus: Teetering Turtle Trouble", 0x76, 0 },
    { "Bonus: Batty Barrel Bandit", 0x20, 0 },
    { "Bonus: Stash Snatch", 0x4A, 0 },
    { "Battle arena: Beaver Brawl", 0x35, 0 },
    // shops
    { "Cranky's Lab", 0x05, 0 },
    { "Funky's Store", 0x01, 0 },
    { "Candy's Music Shop", 0x19, 0 },
    { "Snide's HQ", 0x0F, 0 },
    // arcade games last: the game only takes the next warp once you've quit them
    { "DK Arcade", 0x02, 0 },
    { "Jetpac", 0x09, 0 },
};

inline constexpr int Count = int(sizeof(List) / sizeof(List[0]));

}
