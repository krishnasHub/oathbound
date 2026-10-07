#include "RPGSelfTest.h"
#include "ActionRPG.h"
#include "RPGData.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGPlayerCharacter.h"
#include "RPGPlayerController.h"
#include "SRPGWidgets.h"
#include "GameFramework/GameModeBase.h"
#include "RPGEnemy.h"
#include "RPGLoot.h"
#include "RPGInventoryComponent.h"
#include "RPGAbilityComponent.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Camera/CameraActor.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UObjectIterator.h"

ARPGSelfTest::ARPGSelfTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;   // keeps driving dialogue while the game is paused
}

ARPGPlayerCharacter* ARPGSelfTest::P() const { return URPGSession::Get(this)->Player(); }

ARPGEnemy* ARPGSelfTest::Find(const FString& Type) const
{
	for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == Type && !It->IsDead()) return *It;
	return nullptr;
}

void ARPGSelfTest::Place(const FVector& At, float Yaw)
{
	ARPGPlayerCharacter* Pl = P();
	Pl->SetActorLocation(At + FVector(0, 0, Pl->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 60.f), false, nullptr, ETeleportType::TeleportPhysics);
	Pl->SetActorRotation(FRotator(0, Yaw, 0));
	if (AController* C = Pl->GetController()) C->SetControlRotation(FRotator(-12, Yaw, 0));
}

void ARPGSelfTest::AimAt(const FVector& Point)
{
	ARPGPlayerCharacter* Pl = P();
	const FRotator R = (Point - Pl->GetActorLocation()).GetSafeNormal2D().Rotation();
	if (AController* C = Pl->GetController()) C->SetControlRotation(FRotator(-10, R.Yaw, 0));
}

void ARPGSelfTest::Report(const FString& Line)
{
	UE_LOG(LogRPG, Display, TEXT("[TEST %s t=%.1f] %s"), *Scenario, T, *Line);
}

void ARPGSelfTest::Tick(float Dt)
{
	Super::Tick(Dt);
	T += Dt;
	URPGSession* S = URPGSession::Get(this);
	ARPGPlayerCharacter* Pl = P();
	if (!Pl || T < Next) return;
	const URPGData& D = URPGData::Get(this);
	auto Quit = [&](float After) { Next = T + After; Step = 1000; };
	if (Step == 1000) { Report(TEXT("done")); FPlatformMisc::RequestExit(false); Step = 1001; return; }

	// ---------------------------------------------------------------------------------------------
	if (Scenario == TEXT("combat"))
	{
		if (Step == 0)
		{
			S->Story()->StartQuest(TEXT("slime_cull"));
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(170, 0, 0), 0);
			Report(FString::Printf(TEXT("knight vs slime: slime HP %.0f, player HP %.0f"), Target->Stats->HP, Pl->Stats->HP));
			Step = 1; Next = T + 0.6f;
		}
		else if (Step == 1)
		{
			if (!Target.IsValid() || Target->IsDead())
			{
				Report(FString::Printf(TEXT("slime dead after %d swings. XP %d, level %d, quest %s, stamina %.0f"), Swings, Pl->Xp, Pl->Level(), *S->Story()->ProgressText(TEXT("slime_cull")), Pl->Stats->Stamina));
				Step = 2; Next = T + 2.f;
				return;
			}
			AimAt(Target->GetActorLocation());
			if (FVector::Dist2D(Target->GetActorLocation(), Pl->GetActorLocation()) > 160.f) Place(Target->GetActorLocation() - (Target->GetActorLocation() - Pl->GetActorLocation()).GetSafeNormal2D() * 140.f, Pl->GetActorRotation().Yaw);
			Pl->Stats->Stamina = Pl->Stats->MaxStamina();
			Pl->TestPress(TEXT("Attack"), true);
			Pl->TestPress(TEXT("Attack"), false);
			++Swings;
			Next = T + 0.35f;
			if (T > 25.f) { Report(TEXT("FAIL: slime never died")); Quit(0.5f); }
		}
		else if (Step == 2)
		{
			int32 Pickups = 0;
			for (TActorIterator<ARPGPickup> It(GetWorld()); It; ++It) ++Pickups;
			Report(FString::Printf(TEXT("loot on ground: %d pickups; gold %d"), Pickups, Pl->Inventory->Gold));
			Quit(1.f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("bridge"))
	{
		ARPGEnemy* Brask = nullptr;
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == TEXT("bandit_captain")) Brask = *It;
		if (!Brask) { Report(TEXT("FAIL: no Brask")); Quit(0.5f); return; }
		if (Step == 0)
		{
			Place(Brask->Home + FVector(-280, -390, 0), 35.f);
			AimAt(Brask->GetActorLocation());
			Report(FString::Printf(TEXT("%s: walked up to the bridge: nobody stops us (dialogue open=%d)"), S->Story()->IsDialogueOpen() ? TEXT("FAIL") : TEXT("PASS"), S->Story()->IsDialogueOpen()));
			Step = 10; Next = T + 1.5f;
		}
		else if (Step == 10)
		{
			if (S->Story()->IsDialogueOpen()) { Report(TEXT("FAIL: a dialogue opened by itself")); Quit(0.5f); return; }
			Pl->TryTalk(Brask);   // the player chooses to talk (E + click on Brask)
			Report(TEXT("chose to talk to Brask"));
			Step = 1; Next = T + 2.f;
		}
		else if (Step == 1)
		{
			if (!S->Story()->IsDialogueOpen()) { Report(TEXT("FAIL: talking to Brask did not open the parley")); Quit(0.5f); return; }
			FString Opts;
			for (const FLMChoiceView& V : S->Story()->ChoiceViews) Opts += TEXT("\n      ") + (V.Verb.IsEmpty() ? FString() : TEXT("[") + V.Verb + TEXT("] ")) + V.Text;
			Report(TEXT("parley: ") + S->Story()->SpeakerInfo().Name + TEXT(": ") + S->Story()->DialogueText + Opts);
			Step = 2; Next = T + 2.5f;   // leave the dialogue on screen for a screenshot
		}
		else if (Step == 2)
		{
			const int32 I = S->Story()->FindChoice(TEXT("I am a knight"));
			if (I == INDEX_NONE) { Report(TEXT("FAIL: no honor option")); Quit(0.5f); return; }
			S->Story()->ForcedCheck = true;
			S->Story()->Choose(I);
			Report(TEXT("honor check -> ") + S->Story()->DialogueText);
			const int32 R = S->Story()->FindChoice(TEXT("(Raise"));
			if (R != INDEX_NONE) S->Story()->Choose(R);
			Report(FString::Printf(TEXT("duel started: %s. Others passive: %s"), S->Duel.IsValid() ? TEXT("yes") : TEXT("no"), Find(TEXT("bandit")) && Find(TEXT("bandit"))->IsPassive() ? TEXT("yes") : TEXT("no")));
			Pl->Stats->Base.FindOrAdd(TEXT("might")) += 25.f;   // keep the test short
			Step = 3; Next = T + 0.5f;
		}
		else if (Step == 3)
		{
			if (S->Story()->IsDialogueOpen())
			{
				Report(FString::Printf(TEXT("Brask yields at %.0f/%.0f HP after %d swings: %s"), Brask->Stats->HP, Brask->Stats->MaxHP(), Swings, *S->Story()->DialogueText.Left(60)));
				Step = 29; Next = T + 0.5f;   // a player mid-fight is still clicking: click the game view, then press 1
				return;
			}
			Pl->Stats->HP = Pl->Stats->MaxHP();
			Pl->Stats->Stamina = Pl->Stats->MaxStamina();
			AimAt(Brask->GetActorLocation());
			if (FVector::Dist2D(Brask->GetActorLocation(), Pl->GetActorLocation()) > 170.f) Place(Brask->GetActorLocation() - (Brask->GetActorLocation() - Pl->GetActorLocation()).GetSafeNormal2D() * 150.f, Pl->GetActorRotation().Yaw);
			Pl->TestPress(TEXT("Attack"), true);
			Pl->TestPress(TEXT("Attack"), false);
			++Swings;
			Next = T + 0.35f;
			if (T > 40.f) { Report(TEXT("FAIL: duel never ended")); Quit(0.5f); }
		}
		else if (Step == 29)
		{
			// A left click in the middle of the game view, delivered through Slate like a real mouse.
			TSharedPtr<SWindow> Win = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWindow() : nullptr;
			const FVector2D At = Win ? Win->GetPositionInScreen() + Win->GetSizeInScreen() * FVector2D(0.5f, 0.4f) : FVector2D(640, 300);
			TSet<FKey> Held = { EKeys::LeftMouseButton };
			FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, 0, At, At, Held, EKeys::LeftMouseButton, 0, FModifierKeysState()));
			FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, 0, At, At, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
			Report(TEXT("clicked the game view while the dialogue is open"));
			Step = 30; Next = T + 0.5f;
		}
		else if (Step == 30)
		{
			// Inject "1" through Slate exactly as the OS would deliver it, so this exercises keyboard focus.
			TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
			Report(FString::Printf(TEXT("keyboard focus before pressing 1: %s"), Focus ? *Focus->GetTypeAsString() : TEXT("none")));
			const FKeyEvent Down(EKeys::One, FModifierKeysState(), 0, false, '1', '1');
			FSlateApplication::Get().ProcessKeyDownEvent(Down);
			FSlateApplication::Get().ProcessKeyUpEvent(Down);
			Step = 31; Next = T + 0.7f;   // the choice flashes and fades (~0.4 s) before it's made
		}
		else if (Step == 31)
		{
			Report(FString::Printf(TEXT("after pressing 1: dialogue open=%d, paused=%d"), S->Story()->IsDialogueOpen(), GetWorld()->IsPaused()));
			if (S->Story()->IsDialogueOpen()) { Report(TEXT("FAIL: pressing 1 did not answer the dialogue")); Quit(0.5f); return; }
			Step = 4; Next = T + 2.3f;
		}
		else if (Step == 4)
		{
			Report(FString::Printf(TEXT("after the dialogue: still auto-attacking=%d"), Pl->IsAttacking()));
			int32 Leaving = 0;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == TEXT("bandits") && (It->IsLeaving() || It->IsDead())) ++Leaving;
			const FString* Outcome = S->Story()->Flags.Find(TEXT("toll_outcome"));
			Report(FString::Printf(TEXT("outcome=%s, sabre=%d, bandits leaving=%d/5"), Outcome ? **Outcome : TEXT("none"), Pl->Inventory->Count(TEXT("brask_sabre")), Leaving));
			Quit(1.f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("block"))
	{
		if (Step == 0)
		{
			Target = Find(TEXT("slime"));
			// Just this slime: anything else nearby could join in from the side, which a shield rightly doesn't cover.
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It)
				if (*It != Target.Get() && FVector::Dist2D(It->GetActorLocation(), Target->GetActorLocation()) < 2500.f) It->Destroy();
			Place(Target->GetActorLocation() - FVector(150, 0, 0), 0);
			AimAt(Pl->GetActorLocation() - (Target->GetActorLocation() - Pl->GetActorLocation()));   // aim AWAY: the shield must turn to the threat itself
			Pl->TestPress(TEXT("Secondary"), true);   // raise the shield early (normal block, not perfect)
			Report(FString::Printf(TEXT("shield raised: guarding=%d, HP %.0f, stamina %.0f"), Pl->IsGuarding(), Pl->Stats->HP, Pl->Stats->Stamina));
			Step = 1; Next = T + 1.0f;
		}
		else if (Step == 1)
		{
			// Front-on camera for a screenshot of the raised shield.
			const FVector CamAt = Pl->GetActorLocation() + Pl->GetActorForwardVector() * 60.f + Pl->GetActorRightVector() * -260.f + FVector(0, 0, 30);   // side view from the shield side
			ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamAt, (Pl->Chest() + FVector(0, 0, 15) - CamAt).Rotation());
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->bAutoManageActiveCameraTarget = false; PC->SetViewTarget(Cam); }
			Step = 2; Next = T + 0.6f;
		}
		else if (Step == 2)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/RPG/block.png")), true, false);
			Swings = int32(Pl->Stats->HP);
			Target->State = ERPGEnemyState::Chase;
			Step = 20; Next = T + 0.5f;   // let the slime attack the raised shield
		}
		else if (Step >= 20 && Step < 28)
		{
			static const TCHAR* Names[] = { TEXT("idle"), TEXT("chase"), TEXT("windup"), TEXT("recover"), TEXT("return"), TEXT("leaving") };
			FString Seen;
			for (const FRPGFloater& F : S->Floaters) Seen += F.Text + TEXT(" ");
			Report(FString::Printf(TEXT("slime %s dist=%.0f staggered=%d | HP %.0f stamina %.0f guard=%d | floaters: %s"), Names[uint8(Target->State)],
				FVector::Dist2D(Target->GetActorLocation(), Pl->GetActorLocation()), Target->Tags.Has(TEXT("Staggered")), Pl->Stats->HP, Pl->Stats->Stamina, Pl->IsGuarding(), *Seen));
			++Step; Next = T + 0.5f;
			if (Step == 28) Step = 3;
		}
		else if (Step == 3)
		{
			Report(FString::Printf(TEXT("%s: after the slime's attacks: HP %d -> %.0f (an unblocked hit is ~7; a shield block stops it all), stamina %.0f, guarding=%d"),
				FMath::RoundToInt(Pl->Stats->HP) >= Swings ? TEXT("PASS") : TEXT("FAIL"), Swings, Pl->Stats->HP, Pl->Stats->Stamina, Pl->IsGuarding()));
			Pl->TestPress(TEXT("Secondary"), false);
			Quit(1.f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("walk"))
	{
		// Top-down click-to-move: path around a cottage, click a villager to talk, click a slime to fight.
		auto Ground = [&](float X, float Y)
		{
			FHitResult H;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(TestGround), false, Pl);
			return GetWorld()->LineTraceSingleByChannel(H, FVector(X, Y, 5000.f), FVector(X, Y, -5000.f), ECC_Visibility, Q) ? H.ImpactPoint : FVector(X, Y, 0.f);
		};
		if (Step == 0)
		{
			// The first cottage: start on its north side, click the far (south) side.
			FIntPoint Top(-1, -1);
			for (int32 Y = 1; Y < D.MapH && Top.X < 0; ++Y)
				for (int32 X = 0; X < D.MapW; ++X)
					if (D.Rows[Y][X] == TEXT('H') && D.Rows[Y - 1][X] != TEXT('H')) { Top = FIntPoint(X, Y); break; }
			if (Top.X < 0) { Report(TEXT("FAIL: no house")); Quit(0.5f); return; }
			int32 Bottom = Top.Y;
			while (Bottom + 1 < D.MapH && D.Rows[Bottom + 1][Top.X] == TEXT('H')) ++Bottom;
			const FVector From = D.TileCenter(Top.X, Top.Y - 1), To = D.TileCenter(Top.X, Bottom + 1);
			Place(Ground(From.X, From.Y), 90.f);
			WalkGoal = Ground(To.X, To.Y);
			Report(FString::Printf(TEXT("north of the house at tile (%d,%d); clicking (%d,%d) on its far side"), Top.X, Top.Y - 1, Top.X, Bottom + 1));
			Step = 1; Next = T + 3.f;   // let the runtime navmesh build
		}
		else if (Step == 1)
		{
			Pl->TestClick(WalkGoal);
			Corners = 0;
			Started = T;
			Step = 2; Next = T + 0.2f;
		}
		else if (Step == 2)
		{
			Corners = FMath::Max(Corners, Pl->GetPath().Num());
			if (Pl->GetClickGoal() == ARPGPlayerCharacter::EClickGoal::None)
			{
				const float Miss = FVector::Dist2D(Pl->GetActorLocation(), WalkGoal);
				Report(FString::Printf(TEXT("%s: arrived %.0fuu from the click in %.1fs, path had %d points (%s)"),
					Miss < 120.f ? TEXT("PASS") : TEXT("FAIL"), Miss, T - Started, Corners, Corners > 2 ? TEXT("navmesh, around the house") : TEXT("straight line")));
				Step = 3; Next = T + 0.5f;
			}
			else if (T - Started > 15.f) { Report(FString::Printf(TEXT("FAIL: still walking after 15s, %.0fuu short (path had %d points)"), FVector::Dist2D(Pl->GetActorLocation(), WalkGoal), Corners)); Step = 3; }
			else Next = T + 0.2f;
		}
		else if (Step == 3)
		{
			ARPGCharacterBase* Elder = nullptr;
			for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It) if (It->TalkKey == TEXT("elder")) Elder = *It;
			if (!Elder) { Report(TEXT("FAIL: no elder")); Quit(0.5f); return; }
			Place(Ground(Elder->GetActorLocation().X + 550.f, Elder->GetActorLocation().Y + 250.f), 180.f);
			Pl->TestClick(Elder->GetActorLocation(), Elder);
			Started = T;
			Step = 4; Next = T + 0.2f;
		}
		else if (Step == 4)
		{
			if (S->Story()->IsDialogueOpen())
			{
				Report(FString::Printf(TEXT("PASS: clicked the elder, walked up, dialogue opened in %.1fs: %s"), T - Started, *S->Story()->SpeakerInfo().Name));
				S->Story()->CloseDialogue();
				Step = 5; Next = T + 0.5f;
			}
			else if (T - Started > 10.f) { Report(TEXT("FAIL: dialogue never opened")); Step = 5; }
			else Next = T + 0.2f;
		}
		else if (Step == 5)
		{
			Target = Find(TEXT("slime"));
			if (!Target.IsValid()) { Report(TEXT("FAIL: no slime")); Quit(0.5f); return; }
			const FVector At = Target->GetActorLocation();
			Place(Ground(At.X - 600.f, At.Y), 0.f);
			Started = T;
			Swings = 0;
			Step = 6; Next = T + 0.3f;
		}
		else if (Step == 6)
		{
			if (!Target.IsValid() || Target->IsDead())
			{
				Report(FString::Printf(TEXT("PASS: click-attacked the slime from 600uu away; dead after %d clicks, %.1fs"), Swings, T - Started));
				Quit(1.f);
				return;
			}
			if (T - Started > 25.f) { Report(TEXT("FAIL: slime never died")); Quit(0.5f); return; }
			Pl->Stats->Stamina = Pl->Stats->MaxStamina();
			Pl->TestClick(Target->GetActorLocation(), Target.Get());
			++Swings;
			Next = T + 0.45f;
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("click"))
	{
		// One real mouse click (move, press, release through Slate, like the OS) on a dialogue choice must answer it.
		ARPGPlayerController* PC = Cast<ARPGPlayerController>(Pl->GetController());
		ARPGCharacterBase* Elder = nullptr;
		for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It) if (It->TalkKey == TEXT("elder")) Elder = *It;
		if (!PC || !Elder) { Report(TEXT("FAIL: no elder")); Quit(0.5f); return; }
		if (Step == 0)
		{
			// Like playing: click on the game view first (the viewport takes the mouse), then the dialogue opens.
			Place(Elder->GetActorLocation() + FVector(160, 140, -90), 0);
			FSlateApplication& App = FSlateApplication::Get();
			const FVector2D Mid = App.GetActiveTopLevelWindow().IsValid() ? App.GetActiveTopLevelWindow()->GetPositionInScreen() + App.GetActiveTopLevelWindow()->GetSizeInScreen() * 0.5f : FVector2D(800, 450);
			TSet<FKey> Held = { EKeys::LeftMouseButton };
			App.ProcessMouseMoveEvent(FPointerEvent(0, 0, Mid, Mid, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()));
			App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, 0, Mid, Mid, Held, EKeys::LeftMouseButton, 0, FModifierKeysState()));
			App.ProcessMouseButtonUpEvent(FPointerEvent(0, 0, Mid, Mid, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
			Step = 10; Next = T + 0.3f;
		}
		else if (Step == 10)
		{
			S->OpenDialogue(Elder);
			Step = 1; Next = T + 1.f;   // let it lay out
		}
		else if (Step == 1)
		{
			if (!S->Story()->IsDialogueOpen() || !PC->GetDialogue()) { Report(TEXT("FAIL: dialogue did not open")); Quit(0.5f); return; }
			ClickNode = S->Story()->DialogueText;
			int32 Choice = INDEX_NONE;
			for (int32 I = 0; I < S->Story()->ChoiceViews.Num() && Choice == INDEX_NONE; ++I) if (S->Story()->ChoiceViews[I].bEnabled) Choice = I;
			const FVector2D At = PC->GetDialogue()->ChoiceScreenCenter(Choice);
			FSlateApplication& App = FSlateApplication::Get();
			App.ProcessMouseMoveEvent(FPointerEvent(0, 0, At, At - FVector2D(4, 0), TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()));
			TSet<FKey> Held = { EKeys::LeftMouseButton };
			App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, 0, At, At, Held, EKeys::LeftMouseButton, 0, FModifierKeysState()));
			App.ProcessMouseButtonUpEvent(FPointerEvent(0, 0, At, At, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
			Report(FString::Printf(TEXT("one click on choice %d (\"%s\") at %.0f,%.0f"), Choice + 1, *S->Story()->ChoiceViews[Choice].Text.Left(40), At.X, At.Y));
			Step = 2; Next = T + 0.12f;
		}
		else if (Step == 2)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/RPG/click_chosen.png")), true, false);
			Step = 3; Next = T + 0.8f;
		}
		else if (Step == 3)
		{
			const bool bMoved = !S->Story()->IsDialogueOpen() || S->Story()->DialogueText != ClickNode;
			Report(FString::Printf(TEXT("%s: a single click answered the dialogue (now: %s)"), bMoved ? TEXT("PASS") : TEXT("FAIL"),
				S->Story()->IsDialogueOpen() ? *S->Story()->DialogueText.Left(60) : TEXT("closed")));
			if (S->Story()->IsDialogueOpen()) S->Story()->CloseDialogue();
			Quit(0.5f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("pause"))
	{
		// Esc: picker first, then the pause menu; Resume; New Game reloads into character select with a fresh story.
		ARPGPlayerController* PC = Cast<ARPGPlayerController>(Pl->GetController());
		if (!PC) { Report(TEXT("FAIL: no controller")); Quit(0.5f); return; }
		const AGameModeBase* GM = GetWorld()->GetAuthGameMode();
		if (Step == 0 && GM && UGameplayStatics::HasOption(GM->OptionsString, TEXT("RPGNewGame")))
		{
			// The second world, after New Game.
			Report(FString::Printf(TEXT("%s: after New Game: character select=%d, paused=%d, quests started=%d"),
				PC->IsInCharSelect() && !UGameplayStatics::IsGamePaused(this) && S->Story()->Quests.Num() == 0 ? TEXT("PASS") : TEXT("FAIL"),
				PC->IsInCharSelect(), UGameplayStatics::IsGamePaused(this), S->Story()->Quests.Num()));
			Quit(0.5f);
			return;
		}
		if (Step == 0)
		{
			S->Story()->StartQuest(TEXT("slime_cull"));   // some progress, so New Game has something to wipe
			Pl->TestPicker(0, true);
			Pl->TestPress(TEXT("Escape"), true);
			Report(FString::Printf(TEXT("%s: Esc with the ability picker open closes the picker only: picker=%d, menu=%d"),
				Pl->PickerSlot() < 0 && !PC->IsPauseMenuOpen() ? TEXT("PASS") : TEXT("FAIL"), Pl->PickerSlot(), PC->IsPauseMenuOpen()));
			Step = 1; Next = T + 0.5f;
		}
		else if (Step == 1)
		{
			Pl->TestPress(TEXT("Escape"), true);
			Report(FString::Printf(TEXT("%s: Esc in game: menu=%d, paused=%d"), PC->IsPauseMenuOpen() && UGameplayStatics::IsGamePaused(this) ? TEXT("PASS") : TEXT("FAIL"),
				PC->IsPauseMenuOpen(), UGameplayStatics::IsGamePaused(this)));
			Step = 2; Next = T + 0.6f;
		}
		else if (Step == 2)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/RPG/pause.png")), true, false);
			Step = 3; Next = T + 1.f;
		}
		else if (Step == 3)
		{
			PC->ResumeGame();
			Report(FString::Printf(TEXT("%s: Resume: menu=%d, paused=%d"), !PC->IsPauseMenuOpen() && !UGameplayStatics::IsGamePaused(this) ? TEXT("PASS") : TEXT("FAIL"),
				PC->IsPauseMenuOpen(), UGameplayStatics::IsGamePaused(this)));
			Step = 4; Next = T + 0.5f;
		}
		else if (Step == 4)
		{
			Report(FString::Printf(TEXT("New Game (quests before: %d)..."), S->Story()->Quests.Num()));
			PC->OpenPauseMenu();
			PC->NewGame();   // reloads the world; this scenario resumes in the new one (see the top)
			Step = 5;
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("smoke"))
	{
		// Smoke Bomb: foes in the blast stagger; the cloud lasts its duration, then clears.
		auto Clouds = [this]()
		{
			int32 N = 0;
			for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
				if (It->GetWorld() == GetWorld() && It->IsActive() && It->Template && It->Template->GetName() == TEXT("P_Smoke")) ++N;
			return N;
		};
		if (Step == 0)
		{
			while (Pl->Level() < 2) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
			Target = Find(TEXT("slime"));
			if (!Target.IsValid()) { Report(TEXT("FAIL: no slime")); Quit(0.5f); return; }
			Place(Target->GetActorLocation() - FVector(150, 0, 0), 0.f);
			AimAt(Target->GetActorLocation());
			Step = 1; Next = T + 0.5f;
		}
		else if (Step == 1)
		{
			Pl->Stats->Stamina = Pl->Stats->MaxStamina();
			Pl->TestPress(TEXT("Ability2"), true);
			Started = T;
			Report(FString::Printf(TEXT("%s: smoke bomb thrown; slime staggered=%d, player hidden=%d"),
				Target.IsValid() && Target->Tags.Has(TEXT("Staggered")) ? TEXT("PASS") : TEXT("FAIL"),
				Target.IsValid() && Target->Tags.Has(TEXT("Staggered")), Pl->Tags.Has(TEXT("Hidden"))));
			Step = 2; Next = T + 1.5f;
		}
		else if (Step == 2)
		{
			const int32 N = Clouds();
			Report(FString::Printf(TEXT("%s: %.1fs in: %d smoke emitters puffing"), N > 0 ? TEXT("PASS") : TEXT("FAIL"), T - Started, N));
			Step = 3; Next = Started + 6.f;   // duration 3 s + 2 s to drift off, + margin
		}
		else if (Step == 3)
		{
			const int32 N = Clouds();
			Report(FString::Printf(TEXT("%s: %.1fs in: %d smoke emitters left (cooldown left %.1fs)"), N == 0 ? TEXT("PASS") : TEXT("FAIL"),
				T - Started, N, Pl->Abilities->Cooldowns.FindRef(TEXT("smoke_bomb"))));
			Quit(0.5f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("picker"))
	{
		// Shift + wheel ability picker (slow motion, cycle, cast) and E talk mode vs click-to-fight.
		const float Dil = UGameplayStatics::GetGlobalTimeDilation(this);
		if (Step == 0)
		{
			while (Pl->Level() < 4) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
			Pl->Stats->Mana = Pl->Stats->MaxMana();
			Target = Find(TEXT("slime"));
			if (Target.IsValid()) { Place(Target->GetActorLocation() - FVector(700, 0, 0), 0.f); AimAt(Target->GetActorLocation()); }
			Pl->TestPicker(0, true);
			Report(FString::Printf(TEXT("%s: Shift+wheel opened the picker on slot %d (time dilation was %.2f)"), Pl->PickerSlot() >= 0 ? TEXT("PASS") : TEXT("FAIL"), Pl->PickerSlot(), Dil));
			Step = 1; Next = T + 0.3f;   // (real seconds pass slower now; Next is in game time)
		}
		else if (Step == 1)
		{
			Report(FString::Printf(TEXT("%s: while open, time dilation %.2f"), Dil < 0.5f ? TEXT("PASS") : TEXT("FAIL"), Dil));
			const int32 Before = Pl->PickerSlot();
			Pl->TestPicker(1, true);
			Report(FString::Printf(TEXT("%s: scrolled one notch: slot %d -> %d"), Pl->PickerSlot() != Before ? TEXT("PASS") : TEXT("FAIL"), Before, Pl->PickerSlot()));
			Step = 2; Next = T + 1.2f;   // leave it up for a screenshot (-RPGShot=1)
		}
		else if (Step == 2)
		{
			const int32 Slot = Pl->PickerSlot();
			const FString Id = Pl->Abilities->Ids.IsValidIndex(Slot) ? Pl->Abilities->Ids[Slot] : FString();
			Pl->TestPickerRelease(true);
			Report(FString::Printf(TEXT("%s: released on %s: cooldown %.1fs, time dilation back to %.2f, picker %d"),
				Pl->Abilities->Cooldowns.FindRef(Id) > 0.f && UGameplayStatics::GetGlobalTimeDilation(this) > 0.99f && Pl->PickerSlot() < 0 ? TEXT("PASS") : TEXT("FAIL"),
				*Id, Pl->Abilities->Cooldowns.FindRef(Id), UGameplayStatics::GetGlobalTimeDilation(this), Pl->PickerSlot()));
			Step = 3; Next = T + 1.f;
		}
		else if (Step == 3)
		{
			// Talk mode on a slime: refused with a reason. Clicking a (still neutral) bandit = fight; E on him = talk.
			const FString SlimeWhy = Target.IsValid() ? Pl->TalkBlocker(Target.Get()) : TEXT("?");
			Report(FString::Printf(TEXT("%s: talk to a slime -> \"%s\""), SlimeWhy.IsEmpty() ? TEXT("FAIL") : TEXT("PASS"), *SlimeWhy));
			ARPGEnemy* Bandit = Find(TEXT("bandit_lt"));   // Wren: a bandit with something to say
			if (!Bandit) { Report(TEXT("FAIL: no Wren")); Quit(0.5f); return; }
			Pl->TestClick(Bandit->GetActorLocation(), Bandit);
			const bool bFight = Pl->GetClickGoal() == ARPGPlayerCharacter::EClickGoal::Attack;
			Pl->ClearHeldInput();   // don't actually start a war
			Pl->TryTalk(Bandit);
			const bool bTalk = Pl->GetClickGoal() == ARPGPlayerCharacter::EClickGoal::Talk;
			Pl->ClearHeldInput();
			Report(FString::Printf(TEXT("%s: click on a neutral bandit = %s; E on him = %s (talk blocker: \"%s\")"), bFight && bTalk ? TEXT("PASS") : TEXT("FAIL"),
				bFight ? TEXT("fight") : TEXT("NOT fight"), bTalk ? TEXT("talk") : TEXT("NOT talk"), *Pl->TalkBlocker(Bandit)));
			Quit(0.5f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("elder"))
	{
		// Walk up to Elder Maren, press E, screenshot the dialogue (the game is paused while talking,
		// so this actor takes the screenshot itself — it keeps ticking when paused).
		ARPGCharacterBase* Elder = nullptr;
		for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It) if (It->TalkKey == TEXT("elder")) Elder = *It;
		if (!Elder) { Report(TEXT("FAIL: no elder")); Quit(0.5f); return; }
		if (Step == 0)
		{
			Place(Elder->GetActorLocation() + FVector(160, 140, -90), 0);
			AimAt(Elder->GetActorLocation());
			Report(FString::Printf(TEXT("near the elder; marker over her head: '%s'"), *S->MarkerFor(Elder)));
			Step = 1; Next = T + 1.2f;
		}
		else if (Step == 1)
		{
			Pl->TestPress(TEXT("Interact"), true);
			FString Opts;
			for (const FLMChoiceView& V : S->Story()->ChoiceViews) Opts += TEXT("\n      ") + (V.Verb.IsEmpty() ? FString() : TEXT("[") + V.Verb + TEXT("] ")) + V.Text;
			Report(FString::Printf(TEXT("dialogue open=%d: %s: %s%s"), S->Story()->IsDialogueOpen(), *S->Story()->SpeakerInfo().Name, *S->Story()->DialogueText, *Opts));
			Step = 2; Next = T + 1.0f;
		}
		else if (Step == 2)
		{
			const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/RPG/elder.png"));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			Step = 3; Next = T + 1.5f;
		}
		else if (Step == 3)
		{
			S->Story()->Choose(S->Story()->FindChoice(TEXT("What's troubling")));
			Report(TEXT("asked about trouble -> ") + S->Story()->DialogueText.Left(80));
			const int32 Accept = S->Story()->ChoiceViews.IndexOfByPredicate([](const FLMChoiceView& V) { return V.Text.StartsWith(TEXT("It shall be done")); });
			S->Story()->Choose(Accept);
			Report(FString::Printf(TEXT("quest toll_bridge: %s, dialogue open=%d"), *S->Story()->QuestStatus(TEXT("toll_bridge")), S->Story()->IsDialogueOpen()));
			Quit(1.f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("mage"))
	{
		if (Step == 0)
		{
			Target = Find(TEXT("archer"));
			Place(Target->Home + FVector(-650, 0, 0), 0);
			Report(FString::Printf(TEXT("mage vs archer: mana %.0f/%.0f, HP %.0f"), Pl->Stats->Mana, Pl->Stats->MaxMana(), Pl->Stats->HP));
			Step = 1; Next = T + 0.5f;
		}
		else if (Step == 1)
		{
			if (!Target.IsValid() || Target->IsDead()) { Report(FString::Printf(TEXT("archer dead. mana %.0f, HP %.0f/%.0f"), Pl->Stats->Mana, Pl->Stats->HP, Pl->Stats->MaxHP())); Quit(2.f); return; }
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Attack"), true);
			Pl->TestPress(TEXT("Attack"), false);
			if (FMath::Fmod(T, 2.f) < 0.5f) Report(FString::Printf(TEXT("archer HP %.0f (%s), mana %.0f, HP %.0f"), Target->Stats->HP, Target->State == ERPGEnemyState::Windup ? TEXT("winding up") : TEXT("-"), Pl->Stats->Mana, Pl->Stats->HP));
			Next = T + 0.5f;
			if (T > 30.f) { Report(TEXT("FAIL: timeout")); Quit(0.5f); }
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("pose"))
	{
		// Screenshots of the shooting poses / holsters for the current class, from a fixed camera around
		// the player (no enemies near the start, so nothing interrupts). Saved/Screenshots/RPG/pose_*.png
		struct FShot { float Wait; float Fwd, Right, Up; const TCHAR* Press; bool bDown; const TCHAR* Name; };
		static const FShot Mage[] = {
			{ 1.0f, 230, 160, 40, nullptr, false, TEXT("pose_mage_idle") },
			{ 0.4f, 230, 160, 40, TEXT("Attack"), true, nullptr },
			{ 0.06f, 230, 160, 40, TEXT("Attack"), false, TEXT("pose_mage_kick") },
			{ 0.3f, 230, 160, 40, nullptr, false, TEXT("pose_mage_cast") },
			{ 0.6f, 20, 260, 30, TEXT("Attack"), true, nullptr },
			{ 0.06f, 20, 260, 30, TEXT("Attack"), false, TEXT("pose_mage_kick_side") },
			{ 0.25f, 20, 260, 30, nullptr, false, TEXT("pose_mage_cast_side") },
			{ 0.5f, -200, 110, 70, TEXT("Attack"), true, nullptr },
			{ 0.06f, -200, 110, 70, TEXT("Attack"), false, TEXT("pose_mage_kick_shoulder") },
		};
		static const FShot Thief[] = {
			{ 1.0f, -230, 150, 50, nullptr, false, TEXT("pose_thief_back") },
			{ 0.6f, 230, -170, 40, TEXT("Secondary"), true, nullptr },
			{ 0.9f, 230, -170, 40, nullptr, false, TEXT("pose_thief_draw") },
			{ 0.3f, -200, 110, 70, nullptr, false, TEXT("pose_thief_draw_shoulder") },
			{ 0.3f, 30, 250, 30, nullptr, false, TEXT("pose_thief_draw_side") },
			{ 0.3f, 30, 250, 30, TEXT("Secondary"), false, nullptr },
			{ 0.07f, 30, 250, 30, nullptr, false, TEXT("pose_thief_release_side") },
			{ 0.35f, 60, 240, 20, nullptr, false, TEXT("pose_thief_belt") },
			{ 2.0f, -230, 150, 50, nullptr, false, TEXT("pose_thief_back_after") },
		};
		const bool bThief = Pl->ClassId == TEXT("thief");
		const FShot* Shots = bThief ? Thief : Mage;
		const int32 N = bThief ? UE_ARRAY_COUNT(Thief) : UE_ARRAY_COUNT(Mage);
		if (Step == 0)
		{
			Place(Pl->GetActorLocation() - FVector(0, 0, Pl->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 60.f), 0);
			Swings = 0; Step = 1; Next = T + 0.5f;
			return;
		}
		if (Swings >= N) { Quit(1.f); return; }
		const FShot& Sh = Shots[Swings];
		const FVector CamAt = Pl->GetActorLocation() + Pl->GetActorForwardVector() * Sh.Fwd + Pl->GetActorRightVector() * Sh.Right + FVector(0, 0, Sh.Up);
		ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(CamAt, (Pl->Chest() + FVector(0, 0, 15) - CamAt).Rotation());
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->bAutoManageActiveCameraTarget = false; PC->SetViewTarget(Cam); }
		if (Step == 1) { Step = 2; Next = T + Sh.Wait; return; }   // let the camera settle, then act
		if (Sh.Press) Pl->TestPress(Sh.Press, Sh.bDown);
		if (Sh.Name)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/RPG") / FString(Sh.Name) + TEXT(".png")), true, false);
			Report(FString::Printf(TEXT("%s (drawing=%d)"), Sh.Name, Pl->IsDrawing()));
		}
		++Swings;
		Next = T + (Swings < N ? Shots[Swings].Wait : 1.f);
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("thief"))
	{
		if (Step == 0)
		{
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(600, 0, 0), 0);
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Secondary"), true);   // draw
			Report(TEXT("drawing the bow"));
			Step = 1; Next = T + 1.0f;
		}
		else if (Step == 1)
		{
			const float Before = Target->Stats->HP;
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Secondary"), false);  // release at full draw
			Report(FString::Printf(TEXT("released full draw at slime (HP %.0f)"), Before));
			Step = 2; Next = T + 1.5f;
		}
		else if (Step == 2)
		{
			Report(FString::Printf(TEXT("slime HP after arrow: %s"), Target.IsValid() && !Target->IsDead() ? *FString::Printf(TEXT("%.0f"), Target->Stats->HP) : TEXT("dead")));
			Target = Find(TEXT("slime"));
			Target->ResetToHome();
			Target->SetActorRotation(FRotator(0, 0, 0));
			Place(Target->GetActorLocation() - FVector(110, 0, 0), 0);   // directly behind it (it faces +X)
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Attack"), true);
			Pl->TestPress(TEXT("Attack"), false);
			Step = 3; Next = T + 1.2f;
		}
		else if (Step == 3)
		{
			int32 Backstabs = 0;
			for (const FRPGFloater& F : S->Floaters) if (F.Text == TEXT("BACKSTAB")) ++Backstabs;
			Report(FString::Printf(TEXT("dagger from behind: backstab floaters=%d, slime %s"), Backstabs, Target->IsDead() ? TEXT("dead") : *FString::Printf(TEXT("HP %.0f"), Target->Stats->HP)));
			Quit(1.f);
		}
	}
}
