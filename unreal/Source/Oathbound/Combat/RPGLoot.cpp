#include "RPGLoot.h"
#include "TSLoot.h"
#include "RPGEnemy.h"
#include "RPGPlayerCharacter.h"
#include "RPGSession.h"
#include "LMStory.h"

void RPGLoot::Drop(ARPGEnemy* E)
{
	UWorld* W = E->GetWorld();
	const FVector At = E->GetActorLocation();
	const TArray<TSharedPtr<FJsonValue>> Gold = TSJson::Arr(E->Def, TEXT("gold"));
	if (Gold.Num() == 2) TSLoot::Spawn(W, At, nullptr, FMath::RandRange(int32(Gold[0]->AsNumber()), int32(Gold[1]->AsNumber())));
	TSLoot::DropTable(W, At, TSJson::Str(E->Def, TEXT("loot")));

	// Quest drop (the relic): only while the quest isn't turned in and you don't already have it.
	const TSJson::FObj Q = TSJson::Obj(E->Def, TEXT("questDrop"));
	URPGSession* Session = URPGSession::Get(E);
	const ARPGPlayerCharacter* P = Session->Player();
	// (and its "if", e.g. not while the relic is away: stolen and never given back).
	const TSharedPtr<FJsonValue> If = Q ? Q->TryGetField(TEXT("if")) : nullptr;
	if (Q && P && (!If || Session->Story()->CheckCond(If)) && Session->Story()->QuestStatus(TSJson::Str(Q, TEXT("quest"))) != TEXT("turnedIn") && P->Inventory->Count(TSJson::Str(Q, TEXT("item"))) == 0)
	{
		const FTSItem It = UTSInventoryComponent::MakeItem(W, TSJson::Str(Q, TEXT("item")));
		TSLoot::Spawn(W, At, &It, 0);
	}
}
