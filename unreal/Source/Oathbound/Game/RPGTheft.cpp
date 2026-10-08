#include "RPGTheft.h"
#include "Oathbound.h"
#include "RPGSession.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "LMStory.h"
#include "TSData.h"
#include "TSFeedback.h"
#include "TSPerception.h"
#include "TSSleep.h"
#include "TSChannel.h"

#include "EngineUtils.h"

namespace
{
	const FLinearColor TheftGreen(0.25f, 0.76f, 0.56f), TheftRed(1.f, 0.45f, 0.4f), TheftGrey(0.8f, 0.8f, 0.78f);

	TSJson::FObj DefOf(const ATSCharacter* C)
	{
		if (const ARPGEnemy* E = Cast<ARPGEnemy>(C)) return E->Def;
		if (const ARPGNPC* N = Cast<ARPGNPC>(C)) return UTSData::Get(C).Entry(TEXT("npcs"), N->NpcId);
		return nullptr;
	}

	double TheftTuning(const UObject* Ctx, const TCHAR* Key, double Default) { return UTSData::Get(Ctx).Tuning(Key, Default); }
}

namespace RPGTheft
{
	TSJson::FObj Pockets(const ATSCharacter* C) { return TSJson::Obj(DefOf(C), TEXT("pockets")); }

	FString PocketKey(const ATSCharacter* C)
	{
		if (const ARPGEnemy* E = Cast<ARPGEnemy>(C))
		{
			// Several of a kind (bandits, archers): each its own pockets, known by where it was posted.
			const float Tile = UTSData::Get(C).TileSize;
			return FString::Printf(TEXT("robbed:%s@%d,%d"), *E->Type, FMath::FloorToInt(E->Home.X / Tile), FMath::FloorToInt(E->Home.Y / Tile));
		}
		if (const ARPGNPC* N = Cast<ARPGNPC>(C)) return TEXT("robbed:") + N->NpcId;
		return FString();
	}

	bool Notices(const ATSCharacter* C, const ATSCharacter* Hero)
	{
		if (!C || !Hero) return false;
		const UTSData& D = UTSData::Get(C);
		const TSJson::FObj Def = DefOf(C);
		// Its sight as it hunts with (aggro range), at least close up; villagers: the default senses at a few tiles.
		const float Range = D.Px(FMath::Max(TSJson::Num(Def, TEXT("aggro"), 220), 220.0));
		return TSPerception::CanNotice(C, Hero, FTSSenses::Read(C, Def, Range, TEXT("enemyVision")));
	}

	float Reach(const ATSCharacter* Hero, const ATSCharacter* C)
	{
		return UTSData::Get(Hero).Px(TheftTuning(Hero, TEXT("stealReach"), 26)) + C->Radius() + Hero->Radius();
	}

	ATSCharacter* Target(const ARPGPlayerCharacter* P)
	{
		if (!P) return nullptr;
		const ULMStory* L = URPGSession::Get(P)->Story();
		ATSCharacter* Best = nullptr;
		float BestD = BIG_NUMBER;
		for (TActorIterator<ATSCharacter> It(P->GetWorld()); It; ++It)
		{
			ATSCharacter* C = *It;
			if (C == P || C->IsDead() || C->IsLeaving() || !Pockets(C) || L->HasFlag(PocketKey(C)) || !C->CanBeTargeted(P)) continue;
			const float Dist = FVector::Dist2D(C->GetActorLocation(), P->GetActorLocation());
			if (Dist <= Reach(P, C) && Dist < BestD) { BestD = Dist; Best = C; }
		}
		return Best;
	}

	FString Hint(const ATSCharacter* C) { return TSJson::Str(Pockets(C), TEXT("hint"), TEXT("something")); }

	FString Why(const ARPGPlayerCharacter* P, const ATSCharacter* C, bool bIgnoreReach)
	{
		if (!P || !C) return TEXT("Nobody within reach.");
		if (P->ClassId != TEXT("thief")) return TEXT("Only a thief would try that.");
		if (!P->Tags.Has(TEXT("Sneaking"))) return TEXT("Crouch first (Space).");
		const TSJson::FObj Pk = Pockets(C);
		if (!Pk) return TEXT("Nothing worth taking.");
		if (URPGSession::Get(P)->Story()->HasFlag(PocketKey(C))) return TEXT("Already picked clean.");
		// Level floors: a well-guarded prize needs a practised hand (less of one while its owner sleeps).
		const TSJson::FObj Lv = TSJson::Obj(Pk, TEXT("level"));
		const bool bAsleep = UTSSleep::IsAsleep(C);
		const int32 Min = int32(TSJson::Num(Lv, bAsleep ? TEXT("asleep") : TEXT("min"), TSJson::Num(Lv, TEXT("min"), 1)));
		if (P->Level() < Min) return FString::Printf(TEXT("Too well guarded for you yet (level %d%s)."), Min, bAsleep ? TEXT("") : TEXT(", less while it sleeps"));
		if (!bIgnoreReach && FVector::Dist2D(C->GetActorLocation(), P->GetActorLocation()) > Reach(P, C)) return TEXT("Get closer.");
		if (Notices(C, P)) return bAsleep ? TEXT("Too close: it stirs.") : TEXT("It would see you. Get behind it.");
		return FString();
	}

	void Begin(ARPGPlayerCharacter* P, ATSCharacter* C)
	{
		UTSFeedback* Fb = UTSFeedback::Get(P);
		if (const FString No = Why(P, C); !No.IsEmpty()) { Fb->Float(P->Head() + FVector(0, 0, 30), No, TheftGrey, 0.8f); return; }
		const TSJson::FObj Pk = Pockets(C);
		// Nimble fingers: Agility shortens the lift a little.
		const float Agility = P->Stats->Get(TEXT("agility"));
		const float Time = float(TSJson::Num(Pk, TEXT("time"), 1.2)) * FMath::Clamp(1.2f - 0.03f * Agility, 0.6f, 1.f);
		TWeakObjectPtr<ARPGPlayerCharacter> WP = P;
		TWeakObjectPtr<ATSCharacter> WC = C;
		auto StillGood = [WP, WC]() { return WP.IsValid() && WC.IsValid() && !WC->IsDead() && !WC->IsLeaving() && FVector::Dist2D(WC->GetActorLocation(), WP->GetActorLocation()) <= Reach(WP.Get(), WC.Get()) * 1.3f; };
		P->Channel->Start(TEXT("Stealing..."), Time,
			[WP, WC, StillGood]()
			{
				if (WC.IsValid()) WC->Tags.Add(TEXT("PickedAt"), 0.3f);   // a hand in its purse: it lingers where it stands
				return StillGood() && !Notices(WC.Get(), WP.Get());
			},
			[WP, WC]()
			{
				ARPGPlayerCharacter* Pl = WP.Get(); ATSCharacter* Mark = WC.Get();
				if (!Pl || !Mark) return;
				URPGSession* S = URPGSession::Get(Pl);
				ULMStory* L = S->Story();
				const TSJson::FObj Pk = Pockets(Mark);
				L->SetFlag(PocketKey(Mark));
				// The take, as Loom actions (gold, items), then whatever the pockets' "do" sets off.
				TArray<TSharedPtr<FJsonValue>> Acts;
				const TArray<TSharedPtr<FJsonValue>> G = TSJson::Arr(Pk, TEXT("gold"));
				if (G.Num() == 2)
				{
					const TSJson::FObj A = MakeShared<FJsonObject>();
					A->SetNumberField(TEXT("gold"), FMath::RandRange(int32(G[0]->AsNumber()), int32(G[1]->AsNumber())));
					Acts.Add(MakeShared<FJsonValueObject>(A));
				}
				for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(Pk, TEXT("items")))
				{
					const TSJson::FObj I = V->AsObject();
					if (!I || FMath::FRand() > TSJson::Num(I, TEXT("chance"), 1.0)) continue;
					const TSJson::FObj A = MakeShared<FJsonObject>();
					A->SetStringField(TEXT("giveItem"), TSJson::Str(I, TEXT("item")));
					Acts.Add(MakeShared<FJsonValueObject>(A));
				}
				Acts.Append(TSJson::Arr(Pk, TEXT("do")));
				L->RunActions(Acts);
				L->RefreshQuests();
				// Unseen: foes don't change the world; a villager robbed in the night is a small wrong.
				if (TSJson::Bool(Pk, TEXT("villager"))) L->AddMood(-float(TheftTuning(Pl, TEXT("moodTheftVillager"), 1)), TEXT("robbed ") + Mark->DisplayName);
				UTSFeedback::Get(Pl)->Float(Mark->Head() + FVector(0, 0, 30), TSJson::Str(Pk, TEXT("bark"), TEXT("Lifted!")), TheftGreen, 0.9f);
				UE_LOG(LogRPG, Display, TEXT("Stole from %s (%s)"), *Mark->DisplayName, *PocketKey(Mark));
			},
			[WP, WC, StillGood]()
			{
				if (!WP.IsValid()) return;
				UE_LOG(LogRPG, Display, TEXT("Lift broken: mark %s, in reach %d, noticed %d, %.0fuu apart"), WC.IsValid() ? *WC->DisplayName : TEXT("-"),
					StillGood(), WC.IsValid() && Notices(WC.Get(), WP.Get()), WC.IsValid() ? FVector::Dist2D(WC->GetActorLocation(), WP->GetActorLocation()) : -1.f);
				if (WC.IsValid() && StillGood() && Notices(WC.Get(), WP.Get())) Caught(WP.Get(), WC.Get());
				else UTSFeedback::Get(WP.Get())->Float(WP->Head() + FVector(0, 0, 30), TEXT("You back off."), TheftGrey, 0.7f);
			});
		// A mark on the move: the Thief keeps step behind it while he works (the Assassin's Creed / Thief tail).
		P->Channel->FollowActor(C, Reach(P, C) * 1.3f);
	}

	void Caught(ARPGPlayerCharacter* P, ATSCharacter* C)
	{
		URPGSession* S = URPGSession::Get(P);
		ULMStory* L = S->Story();
		const TSJson::FObj Pk = Pockets(C);
		UTSFeedback::Get(P)->Float(C->Head() + FVector(0, 0, 40), TSJson::Str(Pk, TEXT("caughtBark"), TEXT("Thief!")), TheftRed, 1.3f);
		if (UTSSleep* Z = UTSSleep::Of(C)) Z->Wake();
		TSPerception::Reveal(P);
		if (TSJson::Bool(Pk, TEXT("villager")))
		{
			// Word gets round the village: dearer prices, colder words (flag thief_known).
			L->AddMood(-float(TheftTuning(P, TEXT("moodCaughtVillager"), 3)), TEXT("caught robbing ") + C->DisplayName);
			L->SetFlag(TEXT("thief_known"));
			return;
		}
		L->AddMood(-float(TheftTuning(P, TEXT("moodCaught"), 2)), TEXT("caught robbing ") + C->DisplayName);
		if (ARPGEnemy* E = Cast<ARPGEnemy>(C))
		{
			if (!E->FactionId().IsEmpty()) S->SetHostile(E->FactionId(), TSJson::Str(Pk, TEXT("caughtBark"), TEXT("Thief! Get them!")));
			E->Provoke();
		}
	}
}
