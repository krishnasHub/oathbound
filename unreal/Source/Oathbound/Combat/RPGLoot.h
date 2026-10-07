#pragma once

#include "CoreMinimal.h"

class ARPGEnemy;

/** What an enemy drops when it dies: its coins (enemies.<id>.gold), its loot table, and its quest item while the
 *  quest needs it (questDrop). Tessera's TSLoot puts things on the ground. */
namespace RPGLoot
{
	void Drop(ARPGEnemy* Enemy);
}
