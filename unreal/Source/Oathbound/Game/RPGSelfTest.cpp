#include "RPGSelfTest.h"
#include "TSFeedback.h"
#include "Oathbound.h"
#include "TSData.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGPlayerCharacter.h"
#include "TSHeroControl.h"
#include "RPGPlayerController.h"
#include "SRPGWidgets.h"
#include "GameFramework/GameModeBase.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "RPGGhost.h"
#include "TSFX.h"
#include "TSSky.h"
#include "TSDayNight.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "RPGLoot.h"
#include "TSLoot.h"
#include "TSInventory.h"
#include "TSAbilities.h"
#include "TSAmbientLife.h"
#include "TSDressing.h"
#include "TSCharacterEvents.h"
#include "TSInteractable.h"
#include "TSRoutine.h"
#include "TSSleep.h"
#include "RPGWorldBuilder.h"
#include "TSCombat.h"
#include "TSChannel.h"
#include "RPGTheft.h"
#include "TSSprite.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
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

void ARPGSelfTest::RunStep()
{
	URPGSession* S = URPGSession::Get(this);
	ARPGPlayerCharacter* Pl = P();
	if (!Pl) return;
	const UTSData& D = UTSData::Get(this);

	// ---------------------------------------------------------------------------------------------
	if (Scenario == TEXT("combat"))
	{
		if (Step == 0)
		{
			S->Story()->StartQuest(TEXT("slime_cull"));
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(170, 0, 0), 0);
			Report(FString::Printf(TEXT("knight vs slime: slime HP %.0f, player HP %.0f"), Target->Stats->Health(), Pl->Stats->Health()));
			Step = 1; Next = T + 0.6f;
		}
		else if (Step == 1)
		{
			if (!Target.IsValid() || Target->IsDead())
			{
				Report(FString::Printf(TEXT("slime dead after %d swings. XP %d, level %d, quest %s, stamina %.0f"), Swings, Pl->Xp, Pl->Level(), *S->Story()->ProgressText(TEXT("slime_cull")), Pl->Stats->Pool(RPGStat::Stamina).Current));
				int32 Ghosts = 0;
				for (TActorIterator<ARPGGhost> It(GetWorld()); It; ++It) ++Ghosts;
				Report(FString::Printf(TEXT("%s: its ghost rises (%d)"), Ghosts > 0 ? TEXT("PASS") : TEXT("FAIL"), Ghosts));
				Step = 20; Next = T + 1.1f;
				return;
			}
			AimAt(Target->GetActorLocation());
			if (FVector::Dist2D(Target->GetActorLocation(), Pl->GetActorLocation()) > 160.f) Place(Target->GetActorLocation() - (Target->GetActorLocation() - Pl->GetActorLocation()).GetSafeNormal2D() * 140.f, Pl->GetActorRotation().Yaw);
			Pl->Stats->Pool(RPGStat::Stamina).Current = Pl->Stats->Max(RPGStat::Stamina);
			Pl->TestPress(TEXT("Attack"), true);
			Pl->TestPress(TEXT("Attack"), false);
			++Swings;
			Next = T + 0.35f;
			if (T > 25.f) { Report(TEXT("FAIL: slime never died")); Quit(0.5f); }
		}
		else if (Step == 20)
		{
			Shot(TEXT("ghost"));   // mid-rise
			Step = 2; Next = T + 0.9f;
		}
		else if (Step == 2)
		{
			int32 Pickups = 0;
			for (TActorIterator<ATSPickup> It(GetWorld()); It; ++It) ++Pickups;
			Report(FString::Printf(TEXT("loot on ground: %d pickups; gold %d"), Pickups, Pl->Inventory->Currency));
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
			while (Pl->Level() < 3) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);   // a level-1 knight is too green to challenge Brask
			Place(Brask->Home + FVector(-280, -390, 0), 35.f);
			AimAt(Brask->GetActorLocation());
			Report(FString::Printf(TEXT("%s: walked up to the bridge: nobody stops us (dialogue open=%d)"), S->Story()->IsDialogueOpen() ? TEXT("FAIL") : TEXT("PASS"), S->Story()->IsDialogueOpen()));
			Step = 10; Next = T + 1.5f;
		}
		else if (Step == 10)
		{
			if (S->Story()->IsDialogueOpen()) { Report(TEXT("FAIL: a dialogue opened by itself")); Quit(0.5f); return; }
			Pl->Control->TryTalk(Brask);   // the player chooses to talk (E + click on Brask)
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
				Report(FString::Printf(TEXT("Brask yields at %.0f/%.0f HP after %d swings: %s"), Brask->Stats->Health(), Brask->Stats->MaxHealth(), Swings, *S->Story()->DialogueText.Left(60)));
				Step = 29; Next = T + 0.5f;   // a player mid-fight is still clicking: click the game view, then press 1
				return;
			}
			Pl->Stats->Health() = Pl->Stats->MaxHealth();
			Pl->Stats->Pool(RPGStat::Stamina).Current = Pl->Stats->Max(RPGStat::Stamina);
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
			ClickAt(Win ? Win->GetPositionInScreen() + Win->GetSizeInScreen() * FVector2D(0.5f, 0.4f) : FVector2D(640, 300));
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
			Report(FString::Printf(TEXT("shield raised: guarding=%d, HP %.0f, stamina %.0f"), Pl->IsGuarding(), Pl->Stats->Health(), Pl->Stats->Pool(RPGStat::Stamina).Current));
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
			Shot(TEXT("block"));
			Swings = int32(Pl->Stats->Health());
			Target->State = ERPGEnemyState::Chase;
			Step = 20; Next = T + 0.5f;   // let the slime attack the raised shield
		}
		else if (Step >= 20 && Step < 28)
		{
			static const TCHAR* Names[] = { TEXT("idle"), TEXT("chase"), TEXT("windup"), TEXT("recover"), TEXT("return"), TEXT("leaving") };
			FString Seen;
			for (const FTSFloater& F : UTSFeedback::Get(S)->Floaters) Seen += F.Text + TEXT(" ");
			Report(FString::Printf(TEXT("slime %s dist=%.0f staggered=%d | HP %.0f stamina %.0f guard=%d | floaters: %s"), Names[uint8(Target->State)],
				FVector::Dist2D(Target->GetActorLocation(), Pl->GetActorLocation()), Target->Tags.Has(TEXT("Staggered")), Pl->Stats->Health(), Pl->Stats->Pool(RPGStat::Stamina).Current, Pl->IsGuarding(), *Seen));
			++Step; Next = T + 0.5f;
			if (Step == 28) Step = 3;
		}
		else if (Step == 3)
		{
			Report(FString::Printf(TEXT("%s: after the slime's attacks: HP %d -> %.0f (an unblocked hit is ~7; a shield block stops it all), stamina %.0f, guarding=%d"),
				FMath::RoundToInt(Pl->Stats->Health()) >= Swings ? TEXT("PASS") : TEXT("FAIL"), Swings, Pl->Stats->Health(), Pl->Stats->Pool(RPGStat::Stamina).Current, Pl->IsGuarding()));
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
			Pl->Control->TestClick(WalkGoal);
			Corners = 0;
			Started = T;
			Step = 2; Next = T + 0.2f;
		}
		else if (Step == 2)
		{
			Corners = FMath::Max(Corners, Pl->Control->GetPath().Num());
			if (Pl->Control->GetGoal() == ETSClickGoal::None)
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
			Pl->Control->TestClick(Elder->GetActorLocation(), Elder);
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
			Pl->Stats->Pool(RPGStat::Stamina).Current = Pl->Stats->Max(RPGStat::Stamina);
			Pl->Control->TestClick(Target->GetActorLocation(), Target.Get());
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
			ClickAt(WindowCentre());
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
			ClickAt(At);
			Report(FString::Printf(TEXT("one click on choice %d (\"%s\") at %.0f,%.0f"), Choice + 1, *S->Story()->ChoiceViews[Choice].Text.Left(40), At.X, At.Y));
			Step = 2; Next = T + 0.12f;
		}
		else if (Step == 2)
		{
			Shot(TEXT("click_chosen"));
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
			Pl->Control->TestPicker(0, true);
			Pl->TestPress(TEXT("Escape"), true);
			Report(FString::Printf(TEXT("%s: Esc with the ability picker open closes the picker only: picker=%d, menu=%d"),
				Pl->Control->PickerSlot() < 0 && !PC->IsPauseMenuOpen() ? TEXT("PASS") : TEXT("FAIL"), Pl->Control->PickerSlot(), PC->IsPauseMenuOpen()));
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
			Shot(TEXT("pause"));
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
			Pl->Stats->Pool(RPGStat::Stamina).Current = Pl->Stats->Max(RPGStat::Stamina);
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
			Pl->Stats->Pool(RPGStat::Mana).Current = Pl->Stats->Max(RPGStat::Mana);
			Target = Find(TEXT("slime"));
			if (Target.IsValid()) { Place(Target->GetActorLocation() - FVector(700, 0, 0), 0.f); AimAt(Target->GetActorLocation()); }
			Pl->Control->TestPicker(0, true);
			Report(FString::Printf(TEXT("%s: Shift+wheel opened the picker on slot %d (time dilation was %.2f)"), Pl->Control->PickerSlot() >= 0 ? TEXT("PASS") : TEXT("FAIL"), Pl->Control->PickerSlot(), Dil));
			Step = 1; Next = T + 0.3f;   // (real seconds pass slower now; Next is in game time)
		}
		else if (Step == 1)
		{
			Report(FString::Printf(TEXT("%s: while open, time dilation %.2f"), Dil < 0.5f ? TEXT("PASS") : TEXT("FAIL"), Dil));
			const int32 Before = Pl->Control->PickerSlot();
			Pl->Control->TestPicker(1, true);
			Report(FString::Printf(TEXT("%s: scrolled one notch: slot %d -> %d"), Pl->Control->PickerSlot() != Before ? TEXT("PASS") : TEXT("FAIL"), Before, Pl->Control->PickerSlot()));
			Step = 2; Next = T + 1.2f;   // leave it up for a screenshot (-RPGShot=1)
		}
		else if (Step == 2)
		{
			const int32 Slot = Pl->Control->PickerSlot();
			const FString Id = Pl->Abilities->Ids.IsValidIndex(Slot) ? Pl->Abilities->Ids[Slot] : FString();
			Pl->Control->ClosePicker(true);
			Report(FString::Printf(TEXT("%s: released on %s: cooldown %.1fs, time dilation back to %.2f, picker %d"),
				Pl->Abilities->Cooldowns.FindRef(Id) > 0.f && UGameplayStatics::GetGlobalTimeDilation(this) > 0.99f && Pl->Control->PickerSlot() < 0 ? TEXT("PASS") : TEXT("FAIL"),
				*Id, Pl->Abilities->Cooldowns.FindRef(Id), UGameplayStatics::GetGlobalTimeDilation(this), Pl->Control->PickerSlot()));
			Step = 3; Next = T + 1.f;
		}
		else if (Step == 3)
		{
			// Talk mode on a slime: refused with a reason. Clicking a (still neutral) bandit = fight; E on him = talk.
			const FString SlimeWhy = Target.IsValid() ? Pl->TalkBlocker(Target.Get()) : TEXT("?");
			Report(FString::Printf(TEXT("%s: talk to a slime -> \"%s\""), SlimeWhy.IsEmpty() ? TEXT("FAIL") : TEXT("PASS"), *SlimeWhy));
			ARPGEnemy* Bandit = Find(TEXT("bandit_lt"));   // Wren: a bandit with something to say
			if (!Bandit) { Report(TEXT("FAIL: no Wren")); Quit(0.5f); return; }
			Pl->Control->TestClick(Bandit->GetActorLocation(), Bandit);
			const bool bFight = Pl->Control->GetGoal() == ETSClickGoal::Attack;
			Pl->ClearHeldInput();   // don't actually start a war
			Pl->Control->TryTalk(Bandit);
			const bool bTalk = Pl->Control->GetGoal() == ETSClickGoal::Talk;
			Pl->ClearHeldInput();
			Report(FString::Printf(TEXT("%s: click on a neutral bandit = %s; E on him = %s (talk blocker: \"%s\")"), bFight && bTalk ? TEXT("PASS") : TEXT("FAIL"),
				bFight ? TEXT("fight") : TEXT("NOT fight"), bTalk ? TEXT("talk") : TEXT("NOT talk"), *Pl->TalkBlocker(Bandit)));
			Quit(0.5f);
		}
	}
	// ---------------------------------------------------------------------------------------------
	else if (Scenario == TEXT("frost"))
	{
		if (Step == 0)
		{
			while (Pl->Level() < 2) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
			Pl->Stats->Pool(RPGStat::Mana).Current = Pl->Stats->Max(RPGStat::Mana);
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(220, 0, 0), 0.f);
			// A villager walks over to watch (teleported next to the mage, on the far side from the slime).
			for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) { Other = *It; break; }
			if (Other.IsValid()) Other->SetActorLocation(Pl->GetActorLocation() + FVector(-200, 120, 0), false, nullptr, ETeleportType::TeleportPhysics);
			Step = 1; Next = T + 0.4f;
		}
		else if (Step == 1)
		{
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Ability2"), true);   // Frost Nova
			Started = T;
			Report(FString::Printf(TEXT("cast Frost Nova (cooldown now %.1fs)"), Pl->Abilities->Cooldowns.FindRef(TEXT("frost_nova"))));
			Step = 2; Next = T + 0.3f;
		}
		else if (Step == 2)
		{
			Shot(TEXT("frost_nova"));   // the sphere swelling, the ground frozen
			Step = 3; Next = T + 0.3f;
		}
		else if (Step == 3)
		{
			const bool bSlime = Target.IsValid() && Target->IsFrozen();
			const bool bVillager = Other.IsValid() && Other->IsFrozen();
			Report(FString::Printf(TEXT("%s: the slime is frozen (tint %s)"), bSlime ? TEXT("PASS") : TEXT("FAIL"), Target.IsValid() ? *Target->StatusTint().ToString() : TEXT("-")));
			Report(FString::Printf(TEXT("%s: the villager (%s) is frozen too"), bVillager ? TEXT("PASS") : TEXT("FAIL"), Other.IsValid() ? *Other->DisplayName : TEXT("none")));
			Report(FString::Printf(TEXT("%s: %s can't be talked to while frozen"), Other.IsValid() && !Pl->TalkBlocker(Other.Get()).IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), Other.IsValid() ? *Other->DisplayName : TEXT("-")));
			HeldAt = Target.IsValid() ? Target->GetActorLocation() : FVector::ZeroVector;
			Step = 4; Next = T + 1.5f;
		}
		else if (Step == 4)
		{
			const float Moved = Target.IsValid() ? FVector::Dist2D(Target->GetActorLocation(), HeldAt) : 999.f;
			Report(FString::Printf(TEXT("%s: frozen slime stayed put (moved %.0fuu in 1.5s, 220uu from the mage)"), Moved < 5.f ? TEXT("PASS") : TEXT("FAIL"), Moved));
			Step = 5; Next = T + 1.8f;
		}
		else if (Step == 5)
		{
			const bool bThawed = Target.IsValid() && !Target->IsFrozen() && Target->StatusTint().Equals(FLinearColor::White);
			if (!bThawed && T - Started < 6.f) { Next = T + 0.25f; return; }   // (3s of game time; allow for slow frames)
			int32 Cracks = 0;
			for (TActorIterator<ATSFX> It(GetWorld()); It; ++It) if (It->FindComponentByClass<UProceduralMeshComponent>()) ++Cracks;
			Shot(TEXT("frost_cracks"));
			Report(FString::Printf(TEXT("cracks left in the ground: %s (each nova: 75%% chance, a new pattern)"), Cracks ? TEXT("yes") : TEXT("none this time")));
			Report(FString::Printf(TEXT("%s: thawed after 3s (frozen left %.2fs, dead %d, tint %s)"), bThawed ? TEXT("PASS") : TEXT("FAIL"),
				Target.IsValid() ? Target->Tags.Map.FindRef(TEXT("Frozen")) : -1.f, Target.IsValid() && Target->IsDead(), Target.IsValid() ? *Target->StatusTint().ToString() : TEXT("-")));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("scars"))
	{
		// Spells mark the ground: fireballs scorch it, chain lightning burns forks under its targets (random each time).
		auto Scars = [this]() { int32 N = 0; for (TActorIterator<ATSFX> It(GetWorld()); It; ++It) if (It->FindComponentByClass<UProceduralMeshComponent>()) ++N; return N; };
		if (Step == 0)
		{
			while (Pl->Level() < 4) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(420, 0, 0), 0.f);
			Swings = 0;
			Step = 1; Next = T + 0.5f;
		}
		else if (Step == 1 && Swings < 3)
		{
			Pl->Stats->Pool(RPGStat::Mana).Current = Pl->Stats->Max(RPGStat::Mana);
			Pl->Abilities->Cooldowns.Reset();
			Target = Find(TEXT("slime"));
			if (Target.IsValid()) AimAt(Target->GetActorLocation());
			Pl->TestPress(Swings == 1 ? TEXT("Ability4") : TEXT("Ability1"), true);   // fireball, chain lightning, fireball
			++Swings;
			Next = T + 0.9f;
		}
		else if (Step == 1)
		{
			Shot(TEXT("scars"));
			Report(FString::Printf(TEXT("%s: marks left on the ground: %d (each spell: a chance, a new pattern)"), Scars() > 0 ? TEXT("PASS") : TEXT("FAIL"), Scars()));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("ghost"))
	{
		// Run with -RPGGhostScared: the slime's ghost spots the hero and flees the other way.
		if (Step == 0)
		{
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(200, 0, 0), 0.f);
			Target->Stats->Health() = 1.f;
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Ability1"), true);   // a shield bash finishes it
			Step = 1; Next = T + 0.2f;
		}
		else if (Step == 1)
		{
			if (Target.IsValid() && !Target->IsDead()) { Target->Die(Pl); }
			Step = 2; Next = T + 1.7f;   // in its fright, looking at the hero
		}
		else if (Step == 2)
		{
			Shot(TEXT("ghost_scared"));   // frightened, eyes on the hero
			Step = 3; Next = T + 1.4f;   // near the end of its flight
		}
		else if (Step == 3)
		{
			const ARPGGhost* Gh = nullptr;
			for (TActorIterator<ARPGGhost> It(GetWorld()); It; ++It) Gh = *It;
			const float Away = Gh ? FVector::Dist2D(Gh->GetActorLocation(), Pl->GetActorLocation()) : 0.f;
			Report(FString::Printf(TEXT("%s: the ghost got scared and fled from the hero (scared %d, drifted %.0fuu, now %.0fuu from the hero)"),
				Gh && Gh->IsScared() && Gh->Drift() > 300.f ? TEXT("PASS") : TEXT("FAIL"), Gh ? Gh->IsScared() : 0, Gh ? Gh->Drift() : 0.f, Away));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("talk"))
	{
		// Languages: who each class can talk to; a skeleton waits for the one who speaks its tongue; Brask's level gate
		// (too green at level 1, open again after levelling); peaceful wins pay XP.
		if (Step == 0)
		{
			ARPGEnemy* Slime = Find(TEXT("slime"));
			ARPGEnemy* Archer = Find(TEXT("archer"));
			ARPGEnemy* Brute = Find(TEXT("brute"));
			ARPGEnemy* Wren = Find(TEXT("bandit_lt"));
			if (!Slime || !Archer || !Brute || !Wren) { Report(TEXT("FAIL: missing a slime, archer, brute or Wren")); Quit(0.5f); return; }
			// class -> can it talk to: slime, archer (Grave-speech), brute (Old Tongue), Wren (common)
			struct FRow { const TCHAR* Class; bool Slime, Archer, Brute, Wren; };
			const FRow Rows[] = { { TEXT("knight"), false, false, false, true }, { TEXT("thief"), false, false, false, true },
				{ TEXT("mage"), false, false, true, true }, { TEXT("scholar"), false, true, true, true } };
			for (const FRow& R : Rows)
			{
				Pl->ApplyClass(R.Class, TEXT("male"));
				auto Can = [&](ARPGEnemy* E) { return Pl->TalkBlocker(E).IsEmpty(); };
				const bool bOk = Can(Slime) == R.Slime && Can(Archer) == R.Archer && Can(Brute) == R.Brute && Can(Wren) == R.Wren;
				Report(FString::Printf(TEXT("%s: %s talks to slime %d, skeleton %d, brute %d, Wren %d (skeleton: \"%s\"; slime: \"%s\")"),
					bOk ? TEXT("PASS") : TEXT("FAIL"), R.Class, Can(Slime), Can(Archer), Can(Brute), Can(Wren),
					*Pl->TalkBlocker(Archer), *Pl->TalkBlocker(Slime)));
				const bool bWaits = Archer->IsPassive();
				Report(FString::Printf(TEXT("%s: as a %s, the skeleton %s"), bWaits == R.Archer ? TEXT("PASS") : TEXT("FAIL"), R.Class, bWaits ? TEXT("waits to hear you out") : TEXT("attacks")));
			}
			// Brask: a level-1 knight's challenge is laughed off, and the option comes back after levelling.
			Pl->ApplyClass(TEXT("knight"), TEXT("male"));
			ARPGEnemy* Brask = Find(TEXT("bandit_captain"));
			S->OpenDialogue(Brask);
			const int32 Honor = S->Story()->FindChoice(TEXT("I am a knight"));
			if (Honor == INDEX_NONE) { Report(TEXT("FAIL: no honour challenge at Brask")); Quit(0.5f); return; }
			S->Story()->Choose(Honor);
			const bool bGreen = S->Story()->DialogueText.Contains(TEXT("pup"));
			Report(FString::Printf(TEXT("%s: level %d knight is too green (\"%s\")"), bGreen ? TEXT("PASS") : TEXT("FAIL"), Pl->Level(), *S->Story()->DialogueText.Left(60)));
			S->Story()->CloseDialogue();
			S->OpenDialogue(Brask);
			Report(FString::Printf(TEXT("%s: until levelling up, the challenge is gone"), S->Story()->FindChoice(TEXT("I am a knight")) == INDEX_NONE ? TEXT("PASS") : TEXT("FAIL")));
			S->Story()->CloseDialogue();
			while (Pl->Level() < 4) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
			S->OpenDialogue(Brask);
			Report(FString::Printf(TEXT("%s: at level %d it can be tried again"), S->Story()->FindChoice(TEXT("I am a knight")) != INDEX_NONE ? TEXT("PASS") : TEXT("FAIL"), Pl->Level()));
			S->Story()->CloseDialogue();
			// Peaceful win: the skeleton, talked down, walks off and pays at least its kill XP.
			Pl->ApplyClass(TEXT("scholar"), TEXT("male"));
			auto TotalXp = [Pl]() { int32 X = Pl->Xp; for (int32 L = 1; L < Pl->Level(); ++L) X += ARPGPlayerCharacter::XpToNext(Pl, L); return X; };
			const int32 Before = TotalXp();
			Archer->Leave();
			const int32 After = TotalXp();
			const int32 Kill = int32(TSJson::Num(Archer->Def, TEXT("xp"), 0));
			Report(FString::Printf(TEXT("%s: talked down, it pays %d XP (killing it: %d)"), After - Before >= Kill ? TEXT("PASS") : TEXT("FAIL"), After - Before, Kill));
			Quit(1.f);
		}
	}
	else if (Scenario == TEXT("mood"))
	{
		// The world's mood: killing one who could talk darkens it, talking one down brightens it; a new band is shown at
		// the next dawn / dusk (Tessera's ambient life re-weighted); critters flee the hero.
		ULMStory* L = S->Story();
		ATSAmbientLife* Life = nullptr;
		for (TActorIterator<ATSAmbientLife> It(GetWorld()); It; ++It) Life = *It;
		if (Step == 0)
		{
			if (!Life) { Report(TEXT("FAIL: no ambient life in the world")); Quit(0.5f); return; }
			const float M0 = L->Mood;
			ARPGEnemy* Archer = Find(TEXT("archer"));
			ARPGEnemy* Slime = Find(TEXT("slime"));
			ARPGEnemy* Brute = Find(TEXT("brute"));
			if (!Archer || !Slime || !Brute) { Report(TEXT("FAIL: missing a slime, archer or brute")); Quit(0.5f); return; }
			Slime->Die(Pl);
			const float M1 = L->Mood;
			Archer->Die(Pl);
			const float M2 = L->Mood;
			Pl->ApplyClass(TEXT("scholar"), TEXT("male"));
			Brute->Leave();
			const float M3 = L->Mood;
			Report(FString::Printf(TEXT("%s: slime kill leaves the mood (%.0f -> %.0f); killing a skeleton darkens it (%.0f); talking the brute down brightens it (%.0f)"),
				M1 == M0 && M2 < M1 && M3 > M2 ? TEXT("PASS") : TEXT("FAIL"), M0, M1, M2, M3));
			L->AddMood(-40.f, TEXT("test"));
			const int32 Band = L->MoodBand();
			Report(FString::Printf(TEXT("%s: band %d isn't shown until the day turns (still %d)"), Band < 0 && S->ShownMoodBand() == 0 ? TEXT("PASS") : TEXT("FAIL"), Band, S->ShownMoodBand()));
			S->OnDayPhase(ETSDayPhase::Dawn);
			Report(FString::Printf(TEXT("%s: at dawn the world shows band %d: wolves x%.1f, children x%.1f"),
				S->ShownMoodBand() == Band && Life->GetWeight(TEXT("wolf")) > 0.f && Life->GetWeight(TEXT("child_a")) < 1.f ? TEXT("PASS") : TEXT("FAIL"),
				S->ShownMoodBand(), Life->GetWeight(TEXT("wolf")), Life->GetWeight(TEXT("child_a"))));
			L->AddMood(200.f, TEXT("test: redemption"));
			S->OnDayPhase(ETSDayPhase::Dusk);
			Life->FillNow();
			Report(FString::Printf(TEXT("%s: redeemed (band %d): %d children, %d geese, %d butterflies, %d puppies, %d wolves by day"),
				S->ShownMoodBand() == 3 && Life->Count(TEXT("child_a")) >= 3 && Life->Count(TEXT("goose")) >= 5 && Life->Count(TEXT("wolf")) == 0 ? TEXT("PASS") : TEXT("FAIL"),
				S->ShownMoodBand(), Life->Count(TEXT("child_a")) + Life->Count(TEXT("child_b")), Life->Count(TEXT("goose")),
				Life->Count(TEXT("butterfly_a")) + Life->Count(TEXT("butterfly_b")), Life->Count(TEXT("puppy")), Life->Count(TEXT("wolf"))));
			const TArray<FVector> Kids = Life->Positions(TEXT("child_a"));
			if (Kids.Num()) Place(Kids[0] + FVector(500, 0, 0), 180.f);
			Step = 1; Next = T + 1.2f;
		}
		else if (Step == 1)
		{
			Shot(TEXT("mood_bright"));
			const TArray<FVector> Kids = Life->Positions(TEXT("child_a"));
			if (Kids.Num()) Place(Kids[0] + FVector(150, 0, 0), 180.f);   // walk right up to one
			Step = 2; Next = T + 0.6f;
		}
		else if (Step == 2)
		{
			Report(FString::Printf(TEXT("%s: the children run off when the hero comes close"), Life->HasFled(TEXT("child_a")) ? TEXT("PASS") : TEXT("FAIL")));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("skeletons"))
	{
		// The Scholar and the dead: walk up to a grave and read it; each lone skeleton's want; the Grave-Watcher laid to
		// rest; Captain Ossric's band stands down for his signet.
		ULMStory* L = S->Story();
		ATSInteractable* Grave = ATSInteractable::Find(GetWorld(), TEXT("unmarked_grave"));
		auto Pick = [&](const TCHAR* Prefix) { const int32 I = L->FindChoice(Prefix); if (I != INDEX_NONE) L->Choose(I); return I != INDEX_NONE; };
		if (Step == 0)
		{
			if (!Grave) { Report(TEXT("FAIL: no unmarked grave")); Quit(0.5f); return; }
			Place(Grave->GetActorLocation() + FVector(700, 0, 0), 180.f);
			Pl->Control->TryUse(Grave);   // a click on it: walk up and look
			Step = 1; Next = T + 4.f;
		}
		else if (Step == 1)
		{
			Report(FString::Printf(TEXT("%s: clicked from afar, the hero walked to the grave and looked at it (open %d, speaker %s)"),
				L->IsDialogueOpen() && L->Speaker() == Grave ? TEXT("PASS") : TEXT("FAIL"), L->IsDialogueOpen(), L->Speaker() ? *L->Speaker()->GetName() : TEXT("none")));
			L->CloseDialogue();

			// Lone skeletons: each wants something of its own.
			TArray<ARPGEnemy*> Lone;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == TEXT("archer") && !It->IsDead()) Lone.Add(*It);
			TSet<FString> Wants;
			for (ARPGEnemy* E : Lone) Wants.Add(E->Wish);
			Report(FString::Printf(TEXT("%s: %d lone skeletons want %d different things (%s)"), Wants.Num() == 3 ? TEXT("PASS") : TEXT("FAIL"), Lone.Num(), Wants.Num(), *FString::Join(Wants.Array(), TEXT(", "))));
			for (ARPGEnemy* E : Lone)
			{
				if (E->IsLeaving()) continue;
				S->OpenDialogue(E);
				Pick(TEXT("What keeps you here?"));
				if (E->Wish == TEXT("toll"))
				{
					Pl->Inventory->Currency = 25;
					Pick(TEXT("[Pay 10 gold]"));
					Report(FString::Printf(TEXT("%s: paid the ferryman's toll, the skeleton leaves (gold %d)"), E->IsLeaving() && Pl->Inventory->Currency == 15 ? TEXT("PASS") : TEXT("FAIL"), Pl->Inventory->Currency));
				}
				else if (E->Wish == TEXT("riddle"))
				{
					Pick(TEXT("A ghost."));
					const bool bWrong = L->DialogueText.Contains(TEXT("No."));
					Pick(TEXT("Let me try again."));
					Pick(TEXT("Your shadow."));
					Report(FString::Printf(TEXT("%s: a wrong answer, then the riddle solved: it leaves"), bWrong && E->IsLeaving() ? TEXT("PASS") : TEXT("FAIL")));
				}
				else if (E->Wish == TEXT("song"))
				{
					L->ForcedCheck = true;
					Pick(TEXT("(Hum"));
					L->ForcedCheck.Reset();
					Report(FString::Printf(TEXT("%s: a song from the living days, and it goes"), E->IsLeaving() ? TEXT("PASS") : TEXT("FAIL")));
				}
				L->CloseDialogue();
			}

			// The Grave-Watcher.
			ARPGEnemy* Watcher = Find(TEXT("skeleton_watcher"));
			if (!Watcher) { Report(TEXT("FAIL: no Grave-Watcher")); Quit(0.5f); return; }
			S->OpenDialogue(Watcher);
			Pick(TEXT("How can I help"));
			Pick(TEXT("I'll do it."));
			L->CloseDialogue();
			Report(FString::Printf(TEXT("%s: the Grave-Watcher's quest: %s"), L->QuestStatus(TEXT("grave_rites")) == TEXT("active") ? TEXT("PASS") : TEXT("FAIL"), *L->QuestStatus(TEXT("grave_rites"))));
			Pl->ApplyClass(TEXT("knight"), TEXT("male"));
			S->UseInteractable(Grave);
			const bool bKnightCant = L->FindChoice(TEXT("[Grave-speech]")) == INDEX_NONE;
			L->CloseDialogue();
			Pl->ApplyClass(TEXT("scholar"), TEXT("male"));
			S->UseInteractable(Grave);
			Pick(TEXT("[Grave-speech]"));
			L->CloseDialogue();
			ATSInteractable* Marker = ATSInteractable::Find(GetWorld(), TEXT("grave_marker"));
			Report(FString::Printf(TEXT("%s: a knight can't read Grave-speech (%d); the Scholar reads the rites and a marker appears (%d)"),
				bKnightCant && Marker && Marker->IsShown() ? TEXT("PASS") : TEXT("FAIL"), bKnightCant, Marker && Marker->IsShown()));
			const float Mood0 = L->Mood;
			S->OpenDialogue(Watcher);
			Pick(TEXT("It's done."));
			L->CloseDialogue();
			Report(FString::Printf(TEXT("%s: Aldric rests (resting %d, quest %s, mood %+.0f)"), Watcher->IsLeaving() && Watcher->bResting ? TEXT("PASS") : TEXT("FAIL"),
				Watcher->bResting, *L->QuestStatus(TEXT("grave_rites")), L->Mood - Mood0));
			Place(Watcher->GetActorLocation() + FVector(0, 500, 0), -90.f);
			Step = 2; Next = T + 1.6f;
		}
		else if (Step == 2)
		{
			Shot(TEXT("skeleton_rest"));   // Aldric's ghost rising
			// Captain Ossric's band: a knight gets shot at; the Scholar gets heard.
			ARPGEnemy* Cap = Find(TEXT("skeleton_captain"));
			TArray<ARPGEnemy*> Band;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == TEXT("bonewardens")) Band.Add(*It);
			if (!Cap || Band.Num() != 3) { Report(FString::Printf(TEXT("FAIL: the band (%d)"), Band.Num())); Quit(0.5f); return; }
			Pl->ApplyClass(TEXT("knight"), TEXT("male"));
			const bool bKnightAttacked = !Cap->IsPassive();
			Pl->ApplyClass(TEXT("scholar"), TEXT("male"));
			Report(FString::Printf(TEXT("%s: the Bonewardens attack a knight (%d) but wait for the Scholar (%d)"), bKnightAttacked && Cap->IsPassive() ? TEXT("PASS") : TEXT("FAIL"), bKnightAttacked, Cap->IsPassive()));
			S->OpenDialogue(Cap);
			Pick(TEXT("Why can't you leave?"));
			Pick(TEXT("I'll find it."));
			L->CloseDialogue();
			ATSInteractable* Ring = ATSInteractable::Find(GetWorld(), TEXT("lost_signet"));
			S->UseInteractable(Ring);
			Pick(TEXT("Take it."));
			L->CloseDialogue();
			Report(FString::Printf(TEXT("%s: found the signet (have %d, still lying there %d, quest %s)"), Pl->Inventory->Count(TEXT("signet")) == 1 && Ring && !Ring->IsShown() ? TEXT("PASS") : TEXT("FAIL"),
				Pl->Inventory->Count(TEXT("signet")), Ring && Ring->IsShown(), *L->QuestStatus(TEXT("lost_signet"))));
			S->OpenDialogue(Cap);
			Pick(TEXT("[Give the signet]"));
			L->CloseDialogue();
			int32 Gone = 0;
			for (ARPGEnemy* E : Band) Gone += E->IsLeaving();
			Report(FString::Printf(TEXT("%s: the captain stands his band down: %d of 3 leave (signet given %d, quest %s)"), Gone == 3 && Pl->Inventory->Count(TEXT("signet")) == 0 ? TEXT("PASS") : TEXT("FAIL"),
				Gone, Pl->Inventory->Count(TEXT("signet")) == 0, *L->QuestStatus(TEXT("lost_signet"))));
			Quit(1.f);
		}
	}
	else if (Scenario == TEXT("brute"))
	{
		// The Ruin Brute: too strong / too simple for green heroes; the food chain (Maren's slime cull -> bread -> relic).
		ULMStory* L = S->Story();
		auto Pick = [&](const TCHAR* Prefix) { const int32 I = L->FindChoice(Prefix); if (I != INDEX_NONE) L->Choose(I); return I != INDEX_NONE; };
		ARPGEnemy* Brute = Find(TEXT("brute"));
		ARPGNPC* Elder = nullptr;
		for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) if (It->DialogueRoot == TEXT("elder_root")) Elder = *It;
		if (!Brute || !Elder) { Report(TEXT("FAIL: no brute or elder")); Quit(0.5f); return; }
		// Level gates: a level-1 mage can't get into its head; a level-1 scholar can't teach it.
		Pl->ApplyClass(TEXT("mage"), TEXT("male"));
		Pl->Stats->Pool(TEXT("mana")).Current = 100.f;
		S->OpenDialogue(Brute);
		Pick(TEXT("(Reach into its slow"));
		const bool bStrong = L->DialogueText.Contains(TEXT("wall of stubborn stone"));
		L->CloseDialogue();
		Pl->ApplyClass(TEXT("scholar"), TEXT("male"));
		S->OpenDialogue(Brute);
		Pick(TEXT("There's gold in the old mine"));
		const bool bSimple = L->DialogueText.Contains(TEXT("Talk smaller"));
		L->CloseDialogue();
		Report(FString::Printf(TEXT("%s: at level 1 the mage's hypnosis slides off (%d) and the scholar can't teach it (%d)"), bStrong && bSimple ? TEXT("PASS") : TEXT("FAIL"), bStrong, bSimple));
		// The food chain.
		S->OpenDialogue(Brute);
		Pick(TEXT("What would you take"));
		L->CloseDialogue();
		S->OpenDialogue(Elder);
		Pick(TEXT("Could you spare some food?"));
		Pick(TEXT("I'll do it."));
		L->CloseDialogue();
		Report(FString::Printf(TEXT("%s: the brute wants food (%s); Maren wants slimes culled first (%s)"),
			L->QuestStatus(TEXT("brute_food")) == TEXT("active") && L->QuestStatus(TEXT("maren_slimes")) == TEXT("active") ? TEXT("PASS") : TEXT("FAIL"),
			*L->QuestStatus(TEXT("brute_food")), *L->QuestStatus(TEXT("maren_slimes"))));
		for (int32 I = 0; I < 5; ++I) if (ARPGEnemy* Slime = Find(TEXT("slime"))) Slime->Die(Pl);
		S->OpenDialogue(Elder);
		Pick(TEXT("The slimes are culled."));
		L->CloseDialogue();
		Report(FString::Printf(TEXT("%s: five slimes later, Maren parts with a loaf (bread %d, quest %s)"), Pl->Inventory->Count(TEXT("bread")) == 1 ? TEXT("PASS") : TEXT("FAIL"),
			Pl->Inventory->Count(TEXT("bread")), *L->QuestStatus(TEXT("maren_slimes"))));
		const float Mood0 = L->Mood;
		S->OpenDialogue(Brute);
		Pick(TEXT("[Give the bread]"));
		L->CloseDialogue();
		Report(FString::Printf(TEXT("%s: fed, the brute hands over the relic and settles down (relic %d, pacified %d, passive %d, hunting %d, mood %+.0f)"),
			Pl->Inventory->Count(TEXT("relic")) == 1 && Brute->IsPacified() && Brute->IsPassive() && !Brute->IsHunting() && L->Mood > Mood0 ? TEXT("PASS") : TEXT("FAIL"),
			Pl->Inventory->Count(TEXT("relic")), Brute->IsPacified(), Brute->IsPassive(), Brute->IsHunting(), L->Mood - Mood0));
		S->OpenDialogue(Brute);
		Place(Brute->GetActorLocation() + FVector(0, 650, 0), -90.f);
		Shot(TEXT("brute_fed"));
		Report(FString::Printf(TEXT("%s: now it just snores (\"%s\")"), L->DialogueText.Contains(TEXT("snoring")) ? TEXT("PASS") : TEXT("FAIL"), *L->DialogueText.Left(50)));
		L->CloseDialogue();
		Quit(1.f);
	}
	else if (Scenario == TEXT("trader"))
	{
		// A seasoned Scholar teaches the brute a trade: the relic, a name, and rounds between the mine and the village.
		ULMStory* L = S->Story();
		auto Pick = [&](const TCHAR* Prefix) { const int32 I = L->FindChoice(Prefix); if (I != INDEX_NONE) L->Choose(I); return I != INDEX_NONE; };
		ARPGEnemy* Brute = nullptr;
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == TEXT("brute")) Brute = *It;
		if (!Brute) { Report(TEXT("FAIL: no brute")); Quit(0.5f); return; }
		UTSRoutine* R = Brute->GetRoutine();
		if (Step == 0) { Place(Brute->GetActorLocation() + FVector(0, 500, 0), -90.f); Step = 30; Next = T + 2.f; return; }
		if (Step == 30) { Shot(TEXT("brute_before")); Step = 31; Next = T + 0.3f; return; }
		if (Step == 31)
		{
			auto LevelTo = [&](int32 N) { while (Pl->Level() < N) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp); };   // (ApplyClass starts over at 1)
			// A level-3 mage may now try the hypnosis (it stays offered, and the roll isn't hopeless).
			Pl->ApplyClass(TEXT("mage"), TEXT("male"));
			LevelTo(3);
			S->OpenDialogue(Brute);
			const bool bHypno = L->FindChoice(TEXT("(Reach into its slow")) != INDEX_NONE;
			L->CloseDialogue();
			// A level-1 scholar can't teach it even with luck on their side; a level-4 one can.
			Pl->ApplyClass(TEXT("scholar"), TEXT("male"));
			S->OpenDialogue(Brute);
			L->ForcedCheck = true;
			Pick(TEXT("There's gold in the old mine"));
			const bool bTooSimple = L->DialogueText.Contains(TEXT("Talk smaller")) && !Brute->IsTrader();
			L->CloseDialogue();
			Report(FString::Printf(TEXT("%s: luck can't beat the level floor: at level 1 it's still too simple to teach"), bTooSimple ? TEXT("PASS") : TEXT("FAIL")));
			LevelTo(4);
			S->OpenDialogue(Brute);
			L->ForcedCheck = true;
			Pick(TEXT("There's gold in the old mine"));
			L->ForcedCheck.Reset();
			L->CloseDialogue();
			R = Brute->GetRoutine();
			Report(FString::Printf(TEXT("%s: taught a trade at level %d: relic %d, now \"%s\", a trader %d (the mage could try hypnosis now: %d)"),
				Pl->Inventory->Count(TEXT("relic")) == 1 && Brute->IsTrader() && Brute->DisplayName == TEXT("Grot the Miner") && bHypno ? TEXT("PASS") : TEXT("FAIL"),
				Pl->Level(), Pl->Inventory->Count(TEXT("relic")), *Brute->DisplayName, Brute->IsTrader(), bHypno));
			Pl->ApplyClass(TEXT("knight"), TEXT("male"));
			Report(FString::Printf(TEXT("%s: Grot speaks the common tongue now: even a knight can talk to him (\"%s\")"), Pl->TalkBlocker(Brute).IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *Pl->TalkBlocker(Brute)));
			Place(Brute->GetActorLocation() + FVector(0, 500, 0), -90.f);
			Step = 32; Next = T + 0.5f;
		}
		else if (Step == 32)
		{
			Shot(TEXT("grot_now"));
			Step = 1; Next = T + 22.f;   // a rest in the mine, the walk to its mouth, out with the gold
		}
		else if (Step == 1)
		{
			const FVector Mine = R ? R->Stops[0].At : FVector::ZeroVector;
			const float From = FVector::Dist2D(Brute->GetActorLocation(), Mine);
			Report(FString::Printf(TEXT("%s: after a rest in the mine he comes out of the cave with a sack of gold for the village (carrying %d, %.0fuu from the mine, heading to %s)"),
				Brute->IsCarrying() && From > 200.f && R && R->CurrentTag() == TEXT("village") && !D.AreaAt(Brute->GetActorLocation()) ? TEXT("PASS") : TEXT("FAIL"), Brute->IsCarrying(), From, R ? *R->CurrentTag().ToString() : TEXT("-")));
				Report(FString::Printf(TEXT("%s: Grot walks his rounds awake, not asleep on his feet (asleep %d)"), !UTSSleep::IsAsleep(Brute) ? TEXT("PASS") : TEXT("FAIL"), UTSSleep::IsAsleep(Brute)));
			Place(Brute->GetActorLocation() + FVector(420, 380, 0), -120.f);   // beside him, not in front (the screenshot)
			Step = 2; Next = T + 0.4f;
		}
		else if (Step == 2)
		{
			Shot(TEXT("grot_carrying"));
			Step = 19; Next = T + 0.3f;   // (move on after the shot is taken)
		}
		else if (Step == 19) { Place(Brute->GetActorLocation() + FVector(0, 260, 0), -90.f); Step = 20; Next = T + 0.4f; }
		else if (Step == 20)
		{
			// Watch him in the ruins: standing a while, then walking on with the hero a few steps behind. He must be drawn
			// every time (the occlusion culler once lost flat cards on the ruins floor; tall cards once leaned into walls).
			R->Stop();
			Undrawn = 0;
			Step = 50; Next = T + 1.f;
		}
		else if (Step >= 50 && Step < 58)
		{
			const int32 I = Step - 50;
			if (I == 4) R->Begin();
			if (I >= 4) Place(Brute->GetActorLocation() + FVector(0, 330, 0), -90.f);
			Shot(FString::Printf(TEXT("grot_watch_%d"), I));
			Undrawn += !Brute->Sprite->WasRecentlyRendered(0.2f);
			++Step; Next = T + 1.5f;
			if (Step == 58)
			{
				Report(FString::Printf(TEXT("%s: Grot stays drawn standing and walking through the ruins (missing in %d of 8 looks)"), Undrawn == 0 ? TEXT("PASS") : TEXT("FAIL"), Undrawn));
				Step = 3; Next = T + 0.2f;
			}
		}
		else if (Step == 3) { Quit(1.f); }
	}
	else if (Scenario == TEXT("cave"))
	{
		// The old mine: a door in the ruins leads to a small cave of its own (the brute's lair), and back out again.
		ATSInteractable* Mouth = ATSInteractable::Find(GetWorld(), TEXT("cave_mouth"));
		ATSInteractable* Exit = ATSInteractable::Find(GetWorld(), TEXT("cave_exit"));
		if (!Mouth || !Exit) { Report(TEXT("FAIL: no cave doors")); Quit(0.5f); return; }
		if (Step == 0)
		{
			Place(Mouth->GetActorLocation() + FVector(0, 700, 0), -90.f);
			Pl->Control->TryUse(Mouth);   // click the cave mouth: walk up, go in
			Step = 1; Next = T + 4.f;
		}
		else if (Step == 1)
		{
			const ARPGEnemy* Brute = Find(TEXT("brute"));
			const FTSArea* BruteArea = Brute ? D.AreaAt(Brute->GetActorLocation()) : nullptr;
			Report(FString::Printf(TEXT("%s: through the mine entrance into \"%s\" (the brute lives here: %d)"),
				Pl->AreaId == TEXT("cave") && BruteArea && BruteArea->Id == TEXT("cave") ? TEXT("PASS") : TEXT("FAIL"), *Pl->AreaId, BruteArea != nullptr));
			Place(Exit->GetActorLocation() + FVector(0, -500, 0), 90.f);
			Step = 2; Next = T + 1.5f;
		}
		else if (Step == 2)
		{
			Shot(TEXT("cave"));
			Pl->Control->TryUse(Exit);   // and back out
			Step = 3; Next = T + 3.5f;
		}
		else if (Step == 3)
		{
			const float FromMouth = FVector::Dist2D(Pl->GetActorLocation(), Mouth->GetActorLocation());
			Report(FString::Printf(TEXT("%s: and back out to the ruins (area \"%s\", %.0fuu from the mine entrance)"),
				Pl->AreaId.IsEmpty() && FromMouth < 700.f ? TEXT("PASS") : TEXT("FAIL"), *Pl->AreaId, FromMouth));
			Quit(1.f);
		}
	}
	else if (Scenario == TEXT("palette") || Scenario == TEXT("palette_night"))
	{
		// The world's look at either end of the mood: cracked roads and walls, grumpy men (and at night wolves, bats, snakes);
		// or vines, sunrays, children and puppies; prices and Tobin's tone follow.
		ULMStory* L = S->Story();
		ATSAmbientLife* Life = nullptr;
		for (TActorIterator<ATSAmbientLife> It(GetWorld()); It; ++It) Life = *It;
		ATSDressing* Dress = nullptr;
		for (TActorIterator<ATSDressing> It(GetWorld()); It; ++It) Dress = *It;
		const bool bNight = Scenario == TEXT("palette_night");
		if (!Life || !Dress) { Report(TEXT("FAIL: no ambient life or dressing")); Quit(0.5f); return; }
		auto Show = [&](float Delta) { L->AddMood(Delta, TEXT("test")); S->ApplyMood(); Life->FillNow(); };
		if (Step == 0)
		{
			Show(-200.f);
			if (bNight)
			{
				Report(FString::Printf(TEXT("%s: a dark night: %d wolves, %d bats, %d snakes; no sunrays (%d)"),
					Life->Count(TEXT("wolf")) > 0 && Life->Count(TEXT("bat")) > 0 && Life->Count(TEXT("snake")) > 0 && Dress->Shown(TEXT("sunray")) == 0 ? TEXT("PASS") : TEXT("FAIL"),
					Life->Count(TEXT("wolf")), Life->Count(TEXT("bat")), Life->Count(TEXT("snake")), Dress->Shown(TEXT("sunray"))));
				const TArray<FVector> Wolves = Life->Positions(TEXT("wolf"));
				if (Wolves.Num()) Place(Wolves[0] + FVector(0, 380, 0), -90.f);   // close enough to see them in the hero's light
				Step = 5; Next = T + 1.5f;
				return;
			}
			Report(FString::Printf(TEXT("%s: a dark day: %d road cracks, %d wall cracks, %d moss, %d grumpy men, no vines (%d), no children (%d); a potion costs %d"),
				Dress->Shown(TEXT("road_crack")) > 0 && Dress->Shown(TEXT("crack")) > 0 && Dress->Shown(TEXT("vines")) == 0 && Life->Count(TEXT("child_a")) == 0 && Life->Count(TEXT("grumpy")) > 0 && S->PriceOf(15) > 15 ? TEXT("PASS") : TEXT("FAIL"),
				Dress->Shown(TEXT("road_crack")), Dress->Shown(TEXT("crack")), Dress->Shown(TEXT("moss")), Life->Count(TEXT("grumpy")), Dress->Shown(TEXT("vines")), Life->Count(TEXT("child_a")), S->PriceOf(15)));
			Place(D.TileCenter(8, 9), -90.f);
			Step = 1; Next = T + 5.f;   // textures and shaders settle first (the screenshot)
		}
		else if (Step == 1)
		{
			Shot(TEXT("mood_dark_day"));
			Step = 6; Next = T + 0.4f;   // (brighten after the shot is taken)
		}
		else if (Step == 6)
		{
			Show(400.f);
			Report(FString::Printf(TEXT("%s: a bright day: %d vines, %d sunrays, no road cracks (%d), %d children, %d puppies; a potion costs %d"),
				Dress->Shown(TEXT("vines")) > 0 && Dress->Shown(TEXT("sunray")) > 0 && Dress->Shown(TEXT("road_crack")) == 0 && Life->Count(TEXT("child_a")) > 0 && S->PriceOf(15) < 15 ? TEXT("PASS") : TEXT("FAIL"),
				Dress->Shown(TEXT("vines")), Dress->Shown(TEXT("sunray")), Dress->Shown(TEXT("road_crack")), Life->Count(TEXT("child_a")) + Life->Count(TEXT("child_b")), Life->Count(TEXT("puppy")), S->PriceOf(15)));
			ARPGNPC* Tobin = nullptr;
			for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) if (It->DialogueRoot == TEXT("merchant_root")) Tobin = *It;
			if (Tobin)
			{
				Pl->Inventory->Currency = 100;
				S->OpenDialogue(Tobin);
				const int32 Buy = L->FindChoice(FString::Printf(TEXT("Buy Health Potion (%dg)"), S->PriceOf(15)));
				Report(FString::Printf(TEXT("%s: Tobin's in a good mood (\"%s\") and the potion is %dg"), Buy != INDEX_NONE && L->DialogueText.Contains(TEXT("cheaper")) ? TEXT("PASS") : TEXT("FAIL"), *L->DialogueText.Left(40), S->PriceOf(15)));
				if (Buy != INDEX_NONE) L->Choose(Buy);
				Report(FString::Printf(TEXT("%s: and it costs that (gold %d)"), Pl->Inventory->Currency == 100 - S->PriceOf(15) ? TEXT("PASS") : TEXT("FAIL"), Pl->Inventory->Currency));
				L->CloseDialogue();
			}
			Step = 2; Next = T + 8.f;   // let them wander a while
		}
		else if (Step == 2)
		{
			Shot(TEXT("mood_bright_day"));
			// Nobody walks into the river (or a wall): after a while of wandering, every walker is on open ground.
			int32 Walkers = 0, Wet = 0;
			for (const TCHAR* Kind : { TEXT("child_a"), TEXT("child_b"), TEXT("goose"), TEXT("puppy"), TEXT("grumpy") })
				for (const FVector& At : Life->Positions(Kind)) { ++Walkers; Wet += !Life->CanStand(At); }
			Report(FString::Printf(TEXT("%s: %d walkers about, %d in water or walls"), Walkers > 0 && Wet == 0 ? TEXT("PASS") : TEXT("FAIL"), Walkers, Wet));
			Quit(0.5f);
		}
		else if (Step == 5) { Shot(TEXT("mood_dark_night")); Quit(0.5f); }
	}
	else if (Scenario == TEXT("pacifist"))
	{
		// Proof run: a Scholar who only ever kills slimes clears the bridge, the dead and the relic by talking; the world
		// brightens. (Levels are granted, not ground: the run proves the routes, not the grind.)
		ULMStory* L = S->Story();
		auto Pick = [&](const TCHAR* Prefix) { const int32 I = L->FindChoice(Prefix); if (I != INDEX_NONE) L->Choose(I); return I != INDEX_NONE; };
		auto Talk = [&](ATSCharacter* Who, std::initializer_list<const TCHAR*> Choices) { S->OpenDialogue(Who); for (const TCHAR* Ch : Choices) Pick(Ch); L->CloseDialogue(); };
		ARPGNPC* Elder = nullptr;
		for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) if (It->DialogueRoot == TEXT("elder_root")) Elder = *It;
		int32 TalkersKilled = 0;
		const FDelegateHandle Listen = UTSCharacterEvents::Get(this)->OnDied.AddLambda([&TalkersKilled](ATSCharacter* Who, AActor*) { if (const ARPGEnemy* E = Cast<ARPGEnemy>(Who); E && !E->Speaks().IsEmpty()) ++TalkersKilled; });
		while (Pl->Level() < 5) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
		const float Mood0 = L->Mood;
		// The bridge: talked into honest work.
		S->OpenDialogue(Elder); Pick(TEXT("What's troubling the village?")); L->Choose(0); L->CloseDialogue();
		ARPGEnemy* Brask = Find(TEXT("bandit_captain"));
		S->OpenDialogue(Brask); L->ForcedCheck = true; Pick(TEXT("Ashford needs guards")); Pick(TEXT("(Shake his hand)")); L->CloseDialogue();
		Talk(Elder, { TEXT("The bridge is open.") });
		// The dead: every lone skeleton's want, the Grave-Watcher's grave, the Bonewardens' signet.
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It)
		{
			ARPGEnemy* E = *It;
			if (E->Type != TEXT("archer") || E->IsLeaving()) continue;
			Pl->Inventory->Currency = FMath::Max(Pl->Inventory->Currency, 20);
			S->OpenDialogue(E); Pick(TEXT("What keeps you here?"));
			Pick(TEXT("[Pay 10 gold]")); Pick(TEXT("Your shadow.")); L->ForcedCheck = true; Pick(TEXT("(Hum")); L->ForcedCheck.Reset();
			L->CloseDialogue();
		}
		Talk(Find(TEXT("skeleton_watcher")), { TEXT("How can I help"), TEXT("I'll do it.") });
		S->UseInteractable(ATSInteractable::Find(GetWorld(), TEXT("unmarked_grave"))); Pick(TEXT("[Grave-speech]")); L->CloseDialogue();
		Talk(Find(TEXT("skeleton_watcher")), { TEXT("It's done.") });
		Talk(Find(TEXT("skeleton_captain")), { TEXT("Why can't you leave?"), TEXT("I'll find it.") });
		S->UseInteractable(ATSInteractable::Find(GetWorld(), TEXT("lost_signet"))); Pick(TEXT("Take it.")); L->CloseDialogue();
		Talk(Find(TEXT("skeleton_captain")), { TEXT("[Give the signet]") });
		// The relic: the brute taught a trade.
		Talk(Elder, { TEXT("Is there anything else I can do?") });
		if (L->QuestStatus(TEXT("lost_relic")) == TEXT("none")) L->StartQuest(TEXT("lost_relic"));
		ARPGEnemy* Brute = Find(TEXT("brute"));
		S->OpenDialogue(Brute); L->ForcedCheck = true; Pick(TEXT("There's gold in the old mine")); L->ForcedCheck.Reset(); L->CloseDialogue();
		Talk(Elder, { TEXT("I recovered the relic.") });
		UTSCharacterEvents::Get(this)->OnDied.Remove(Listen);
		int32 Foes = 0;
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (!It->Speaks().IsEmpty() && !It->IsDead() && !It->IsLeaving() && !It->IsPacified() && It->Team == ETSTeam::Hostile) ++Foes;
		const bool bQuests = L->QuestStatus(TEXT("toll_bridge")) == TEXT("turnedIn") && L->QuestStatus(TEXT("lost_relic")) == TEXT("turnedIn")
			&& L->QuestStatus(TEXT("grave_rites")) == TEXT("turnedIn") && L->QuestStatus(TEXT("lost_signet")) == TEXT("turnedIn");
		Report(FString::Printf(TEXT("%s: the bridge %s, the relic %s, the grave %s, the signet %s; talkers killed %d; talkers still hostile %d"),
			bQuests && TalkersKilled == 0 && Foes == 0 ? TEXT("PASS") : TEXT("FAIL"), *L->QuestStatus(TEXT("toll_bridge")), *L->QuestStatus(TEXT("lost_relic")),
			*L->QuestStatus(TEXT("grave_rites")), *L->QuestStatus(TEXT("lost_signet")), TalkersKilled, Foes));
		S->OnDayPhase(ETSDayPhase::Dawn);
		Report(FString::Printf(TEXT("%s: the world brightened (mood %.0f -> %.0f, band %d shown at dawn)"), L->MoodBand() >= 2 && S->ShownMoodBand() == L->MoodBand() ? TEXT("PASS") : TEXT("FAIL"), Mood0, L->Mood, S->ShownMoodBand()));
		Quit(1.f);
	}
	else if (Scenario == TEXT("spree"))
	{
		// Proof run: a knight who cuts down everyone who could have talked darkens the world (wolves by night, grumpy
		// men by day); then a change of heart brings it back - slowly.
		ULMStory* L = S->Story();
		auto Pick = [&](const TCHAR* Prefix) { const int32 I = L->FindChoice(Prefix); if (I != INDEX_NONE) L->Choose(I); return I != INDEX_NONE; };
		ATSAmbientLife* Life = nullptr;
		for (TActorIterator<ATSAmbientLife> It(GetWorld()); It; ++It) Life = *It;
		// Strike a bandit at the bridge (a betrayal while they'd talk), then put every archer and bandit down.
		if (ARPGEnemy* B = Find(TEXT("bandit"))) B->OnStruck(Pl);
		int32 Kills = 0;
		for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It)
			if ((It->Type == TEXT("archer") || It->Type == TEXT("bandit")) && !It->IsDead()) { It->Die(Pl); ++Kills; }
		const int32 Low = L->MoodBand();
		S->OnDayPhase(ETSDayPhase::Dusk);
		Report(FString::Printf(TEXT("%s: %d killed: mood %.0f, band %d; by dusk the world shows it (wolves x%.1f, grumpy men x%.1f, children x%.1f)"),
			Low <= -2 && S->ShownMoodBand() == Low && Life && Life->GetWeight(TEXT("wolf")) > 0.f && Life->GetWeight(TEXT("grumpy")) > 0.f && Life->GetWeight(TEXT("child_a")) == 0.f ? TEXT("PASS") : TEXT("FAIL"),
			Kills, L->Mood, Low, Life ? Life->GetWeight(TEXT("wolf")) : 0.f, Life ? Life->GetWeight(TEXT("grumpy")) : 0.f, Life ? Life->GetWeight(TEXT("child_a")) : 0.f));
		// Redemption: the Scholar's way with the dead who are left.
		const float Before = L->Mood;
		Pl->ApplyClass(TEXT("scholar"), TEXT("male"));
		S->OpenDialogue(Find(TEXT("skeleton_watcher"))); Pick(TEXT("How can I help")); Pick(TEXT("I'll do it.")); L->CloseDialogue();
		S->UseInteractable(ATSInteractable::Find(GetWorld(), TEXT("unmarked_grave"))); Pick(TEXT("[Grave-speech]")); L->CloseDialogue();
		S->OpenDialogue(Find(TEXT("skeleton_watcher"))); Pick(TEXT("It's done.")); L->CloseDialogue();
		S->OpenDialogue(Find(TEXT("skeleton_captain"))); Pick(TEXT("Why can't you leave?")); Pick(TEXT("I'll find it.")); L->CloseDialogue();
		S->UseInteractable(ATSInteractable::Find(GetWorld(), TEXT("lost_signet"))); Pick(TEXT("Take it.")); L->CloseDialogue();
		S->OpenDialogue(Find(TEXT("skeleton_captain"))); Pick(TEXT("[Give the signet]")); L->CloseDialogue();
		const int32 Shown = S->ShownMoodBand();
		S->OnDayPhase(ETSDayPhase::Dawn);
		Report(FString::Printf(TEXT("%s: redemption: mood %.0f -> %.0f (band %d -> %d), shown at dawn, not before (%d until then); slow: not yet back to normal (%d)"),
			L->Mood > Before + 10.f && Shown == Low && S->ShownMoodBand() == L->MoodBand() && L->MoodBand() < 0 ? TEXT("PASS") : TEXT("FAIL"),
			Before, L->Mood, Low, L->MoodBand(), Shown, L->MoodBand()));
		Quit(1.f);
	}
	else if (Scenario.StartsWith(TEXT("tour")))
	{
		// A play-through for whichever class the run picked (-RPGClass): Maren, Brask, the dead, the grave, the old mine
		// and its brute, and back out. Screenshots at each stop (tour_<class>_*); what each class can and can't do.
		ULMStory* L = S->Story();
		const FString Cls = Pl->ClassId;
		auto Snap = [&](const TCHAR* What) { Shot(FString::Printf(TEXT("tour_%s_%s"), *Cls, What)); };
		auto Shown = [&](const TCHAR* Prefix) { return L->FindChoice(Prefix) != INDEX_NONE; };
		ARPGNPC* Elder = nullptr;
		for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) if (It->DialogueRoot == TEXT("elder_root")) Elder = *It;
		ARPGEnemy* Brask = Find(TEXT("bandit_captain"));
		ARPGEnemy* Brute = Find(TEXT("brute"));
		ATSInteractable* Grave = ATSInteractable::Find(GetWorld(), TEXT("unmarked_grave"));
		ATSInteractable* Mouth = ATSInteractable::Find(GetWorld(), TEXT("cave_mouth"));
		ATSInteractable* Exit = ATSInteractable::Find(GetWorld(), TEXT("cave_exit"));
		if (!Elder || !Brask || !Brute || !Grave || !Mouth || !Exit) { Report(TEXT("FAIL: something's missing from the world")); Quit(0.5f); return; }
		if (Step == 0)
		{
			while (Pl->Level() < 4) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
			Place(Elder->GetActorLocation() + FVector(380, 0, 0), 180.f);   // beside her (a house stands south of her)
			Pl->Control->TestClick(Elder->GetActorLocation(), Elder);   // click Maren: walk up and talk
			Step = 1; Next = T + 2.5f;
		}
		else if (Step == 1)
		{
			Report(FString::Printf(TEXT("%s: [%s] clicking Elder Maren opens her conversation (\"%s\")"), L->IsDialogueOpen() && !L->DialogueText.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *Cls, *L->DialogueText.Left(60)));
			Snap(TEXT("1_elder"));
			L->CloseDialogue();
			Step = 11; Next = T + 0.4f;
		}
		else if (Step == 11) { Place(Brask->GetActorLocation() + FVector(-250, -300, 0), 40.f); Step = 2; Next = T + 1.5f; }
		else if (Step == 2)
		{
			S->OpenDialogue(Brask);
			struct FOpt { const TCHAR* Class; const TCHAR* Prefix; };
			const FOpt Opts[] = { { TEXT("knight"), TEXT("I am a knight") }, { TEXT("mage"), TEXT("(Reach into his mind)") }, { TEXT("thief"), TEXT("(Spin a dagger)") }, { TEXT("scholar"), TEXT("Ashford needs guards") } };
			FString Mine, Others;
			for (const FOpt& O : Opts) { if (Cls == O.Class) Mine = Shown(O.Prefix) ? TEXT("offered") : TEXT("MISSING"); else if (Shown(O.Prefix)) Others += O.Class; }
			Report(FString::Printf(TEXT("%s: [%s] at the bridge Brask hears this class's own approach (%s); no other class's (%s)"), Mine == TEXT("offered") && Others.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *Cls, *Mine, Others.IsEmpty() ? TEXT("none") : *Others));
			Snap(TEXT("2_brask"));
			L->CloseDialogue();
			Step = 21; Next = T + 0.4f;
		}
		else if (Step == 21) { ARPGEnemy* Skel = Find(TEXT("archer")); Place(Skel->GetActorLocation() + FVector(0, 420, 0), -90.f); Target = Skel; Step = 3; Next = T + 1.5f; }
		else if (Step == 3)
		{
			ARPGEnemy* Skel = Target.Get();
			const bool bScholar = Cls == TEXT("scholar");
			const FString Why = Skel ? Pl->TalkBlocker(Skel) : FString(TEXT("gone"));
			Report(FString::Printf(TEXT("%s: [%s] a skeleton %s (\"%s\")"), Skel && (Why.IsEmpty() == bScholar) && (Skel->IsPassive() == bScholar) ? TEXT("PASS") : TEXT("FAIL"),
				*Cls, bScholar ? TEXT("waits to be talked to") : TEXT("won't talk, and comes for you"), *Why));
			Snap(TEXT("3_skeleton"));
			Step = 31; Next = T + 0.4f;
		}
		else if (Step == 31) { Place(Grave->GetActorLocation() + FVector(0, 300, 0), -90.f); Step = 4; Next = T + 1.2f; }
		else if (Step == 4)
		{
			S->UseInteractable(Grave);
			const bool bRead = Shown(TEXT("[Grave-speech]"));
			Report(FString::Printf(TEXT("%s: [%s] the unmarked grave: the rites %s"), bRead == (Cls == TEXT("scholar")) ? TEXT("PASS") : TEXT("FAIL"), *Cls, bRead ? TEXT("can be read") : TEXT("are in Grave-speech, not for this hero")));
			Snap(TEXT("4_grave"));
			L->CloseDialogue();
			Step = 41; Next = T + 0.4f;
		}
		else if (Step == 41) { Place(Mouth->GetActorLocation() + FVector(0, 650, 0), -90.f); Pl->Control->TryUse(Mouth); Step = 5; Next = T + 4.f; }
		else if (Step == 5)
		{
			Report(FString::Printf(TEXT("%s: [%s] into the old mine (\"%s\")"), Pl->AreaId == TEXT("cave") ? TEXT("PASS") : TEXT("FAIL"), *Cls, *Pl->AreaId));
			Place(Brute->GetActorLocation() + FVector(-60, 380, 0), -90.f);
			Step = 6; Next = T + 1.5f;
		}
		else if (Step == 6)
		{
			Snap(TEXT("5_cave"));
			const bool bOld = Cls == TEXT("mage") || Cls == TEXT("scholar");
			const FString Why = Pl->TalkBlocker(Brute);
			Report(FString::Printf(TEXT("%s: [%s] the Ruin Brute %s (\"%s\")"), Why.IsEmpty() == bOld ? TEXT("PASS") : TEXT("FAIL"), *Cls, bOld ? TEXT("can be talked to in the Old Tongue") : TEXT("can't be talked to"), *Why));
			if (bOld)
			{
				S->OpenDialogue(Brute);
				const bool bHyp = Shown(TEXT("(Reach into its slow")), bTeach = Shown(TEXT("There's gold in the old mine"));
				Report(FString::Printf(TEXT("%s: [%s] its options: hypnosis %d (mage only), teaching a trade %d (scholar only)"),
					bHyp == (Cls == TEXT("mage")) && bTeach == (Cls == TEXT("scholar")) ? TEXT("PASS") : TEXT("FAIL"), *Cls, bHyp, bTeach));
				Snap(TEXT("6_brute_talk"));
				L->CloseDialogue();
				Step = 8; Next = T + 0.6f;
			}
			else
			{
				// Fight it: a few swings, and it must take the hits.
				Swings = 0;
				Target = Brute;
				Step = 7; Next = T + 0.2f;
			}
		}
		else if (Step == 7)
		{
			ARPGEnemy* B = Target.Get();
			if (B && Swings < 6) { AimAt(B->GetActorLocation()); Pl->Control->TestClick(B->GetActorLocation(), B); ++Swings; Next = T + 0.7f; return; }
			Report(FString::Printf(TEXT("%s: [%s] fighting the brute: it takes the hits (%.0f / %.0f HP)"), B && B->Stats->Health() < B->Stats->MaxHealth() ? TEXT("PASS") : TEXT("FAIL"), *Cls, B ? B->Stats->Health() : 0.f, B ? B->Stats->MaxHealth() : 0.f));
			Snap(TEXT("6_brute_fight"));
			Step = 8; Next = T + 0.5f;
		}
		else if (Step == 8)
		{
			Pl->Stats->Health() = Pl->Stats->MaxHealth();
			Place(Exit->GetActorLocation() + FVector(0, -450, 0), 90.f);
			Pl->Control->TryUse(Exit);
			Step = 9; Next = T + 3.5f;
		}
		else if (Step == 9)
		{
			Report(FString::Printf(TEXT("%s: [%s] and back out to the ruins (\"%s\")"), Pl->AreaId.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *Cls, *Pl->AreaId));
			Snap(TEXT("7_out"));
			Quit(1.f);
		}
	}
	else if (Scenario == TEXT("sleep"))
	{
		// Keeping hours (TODO.md T0): skeletons and the brute sleep by day (the graveyard, the hay), bandits and villagers
		// by night (the camp, their houses). A sleeper only hears a hero right beside it; a hit wakes it, harder. Only
		// the Thief opens a house door; a sleeper indoors can only be reached from inside.
		auto Asleep = [](const ATSCharacter* C) { return UTSSleep::IsAsleep(C); };
		auto Indoors = [](const AActor* A) { const UTSSleep* Z = UTSSleep::Of(A); return Z && Z->IsIndoors(); };
		auto Npc = [this](const FString& Id) -> ARPGNPC* { for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) if (It->NpcId == Id) return *It; return nullptr; };
		// Sleepers by rest place: how many there are, and how many are asleep.
		auto Count = [this](const FString& Rest, int32& Total)
		{
			int32 N = 0; Total = 0;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It)
			{
				if (It->IsDead() || !UTSSleep::Of(*It) || TSJson::Str(It->Def, TEXT("rest")) != Rest) continue;
				++Total; N += UTSSleep::IsAsleep(*It) ? 1 : 0;
			}
			return N;
		};
		ARPGWorldBuilder* B = S->Builder();
		ARPGNPC* Elder = Npc(TEXT("elder"));
		if (!B || !Elder) { Report(TEXT("FAIL: no world builder or no Elder Maren")); Quit(0.5f); return; }
		if (Step == 0) { Step = 1; Next = T + 2.f; return; }   // the first tick puts the day sleepers to bed
		if (Step == 1)
		{
			int32 SkTotal = 0, CampTotal = 0;
			const int32 SkAsleep = Count(TEXT("graveyard"), SkTotal), CampAsleep = Count(TEXT("camp"), CampTotal);
			ARPGEnemy* Sk = Find(TEXT("archer"));
			ARPGEnemy* Brute = Find(TEXT("brute"));
			ARPGEnemy* Brask = Find(TEXT("bandit_captain"));
			Report(FString::Printf(TEXT("%s: by day (%.1fh) %d / %d skeletons sleep in the graveyard (an archer indoors: %s), the brute on its hay (%s)"),
				SkTotal > 0 && SkAsleep == SkTotal && Sk && Indoors(Sk) && Brute && Asleep(Brute) ? TEXT("PASS") : TEXT("FAIL"), ATSSky::Hour(), SkAsleep, SkTotal,
				Sk && Indoors(Sk) ? TEXT("yes") : TEXT("no"), Brute && Asleep(Brute) ? TEXT("yes") : TEXT("no")));
			Report(FString::Printf(TEXT("%s: by day the camp is up (%d / %d asleep) and Elder Maren is about (asleep: %s)"),
				CampAsleep == 0 && Brask && !Asleep(Brask) && !Asleep(Elder) && !Indoors(Elder) ? TEXT("PASS") : TEXT("FAIL"), CampAsleep, CampTotal, Asleep(Elder) ? TEXT("yes") : TEXT("no")));
			if (Sk) Report(FString::Printf(TEXT("%s: a skeleton in the graveyard can't be picked out from outside it"), !Sk->CanBeTargeted(Pl) ? TEXT("PASS") : TEXT("FAIL")));
			ATSSky::SetHour(23.f);
			Step = 2; Next = T + 14.f;   // night falls at once: everyone turns in (the camp is a walk away) or gets up
		}
		else if (Step == 2)
		{
			int32 SkTotal = 0, CampTotal = 0;
			const int32 SkAsleep = Count(TEXT("graveyard"), SkTotal), CampAsleep = Count(TEXT("camp"), CampTotal);
			ARPGEnemy* Brute = Find(TEXT("brute"));
			ARPGEnemy* Wren = Find(TEXT("bandit_lt"));
			ARPGEnemy* Sk = Find(TEXT("archer"));
			Report(FString::Printf(TEXT("%s: at night (%.1fh, phase %d) the skeletons are up (%d asleep, archer outside: %s) and the brute too (%s)"),
				SkAsleep == 0 && Sk && !Indoors(Sk) && Brute && !Asleep(Brute) ? TEXT("PASS") : TEXT("FAIL"), ATSSky::Hour(), int32(UTSDayNight::Get(this)->Phase()),
				SkAsleep, Sk && !Indoors(Sk) ? TEXT("yes") : TEXT("no"), Brute && Asleep(Brute) ? TEXT("asleep") : TEXT("awake")));
			{
				// Up and out by walking, not by popping: most have left the bone-hole and are on their way home.
				int32 Away = 0, Total = 0;
				for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It)
					if (const UTSSleep* Z = UTSSleep::Of(*It); Z && Z->bHasEntry && TSJson::Str(It->Def, TEXT("rest")) == TEXT("graveyard"))
					{
						++Total;
						if (FVector::Dist2D(It->GetActorLocation(), Z->Entry) > 400.f) ++Away;
					}
				Report(FString::Printf(TEXT("%s: at night the skeletons dig out one by one and walk off toward their posts (%d / %d away from the bone-hole)"),
					Total > 0 && Away * 3 >= Total * 2 ? TEXT("PASS") : TEXT("FAIL"), Away, Total));
			}
			Report(FString::Printf(TEXT("%s: the camp sleeps on its bedrolls (%d / %d) while Wren keeps watch (%s)"),
				CampTotal > 0 && CampAsleep == CampTotal && Wren && !Asleep(Wren) ? TEXT("PASS") : TEXT("FAIL"), CampAsleep, CampTotal, Wren && !Asleep(Wren) ? TEXT("awake") : TEXT("asleep")));
			Report(FString::Printf(TEXT("%s: Elder Maren has gone in to bed (asleep %s, indoors %s)"),
				Asleep(Elder) && Indoors(Elder) ? TEXT("PASS") : TEXT("FAIL"), Asleep(Elder) ? TEXT("yes") : TEXT("no"), Indoors(Elder) ? TEXT("yes") : TEXT("no")));
			// A bandit asleep: walk up quietly to within normal hearing (but not right beside it).
			Target = nullptr;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == TEXT("bandit") && Asleep(*It)) { Target = *It; break; }
			if (!Target.IsValid()) { Report(TEXT("FAIL: no sleeping bandit")); Quit(0.5f); return; }
			Place(Target->GetActorLocation() + FVector(0, -D.Px(55), 0), 90.f);
			Step = 3; Next = T + 1.2f;
		}
		else if (Step == 3)
		{
			Shot(TEXT("sleep_camp"));
			const bool bStill = Target.IsValid() && Asleep(Target.Get());
			Report(FString::Printf(TEXT("%s: a hero 55px from a sleeping bandit isn't heard (still asleep: %s)"), bStill ? TEXT("PASS") : TEXT("FAIL"), bStill ? TEXT("yes") : TEXT("no")));
			if (Target.IsValid()) Place(Target->GetActorLocation() + FVector(0, -D.Px(18), 0), 90.f);
			Step = 4; Next = T + 0.8f;
		}
		else if (Step == 4)
		{
			Report(FString::Printf(TEXT("%s: right beside it, the bandit wakes"), Target.IsValid() && !Asleep(Target.Get()) ? TEXT("PASS") : TEXT("FAIL")));
			// Another sleeper, struck: a heavier blow, and it wakes.
			ARPGEnemy* Struck = nullptr;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == TEXT("bandit") && Asleep(*It) && *It != Target.Get()) { Struck = *It; break; }
			if (Struck)
			{
				FTSHit Hit; Hit.Base = 6.f;
				const float K = float(Struck->Stats->Rule(TEXT("armorConstant"), 100));
				const float Plain = Hit.Base * K / (K + Struck->Stats->Armor());
				const float Before = Struck->Stats->Health();
				TSCombat::Deal(Pl, Struck, Hit);
				const float Took = Before - Struck->Stats->Health();
				Report(FString::Printf(TEXT("%s: a sleeping bandit struck takes %.0f (awake: ~%.0f) and wakes"), Took >= Plain * 1.6f && !Asleep(Struck) ? TEXT("PASS") : TEXT("FAIL"), Took, Plain));
			}
			else Report(TEXT("FAIL: no second sleeping bandit"));
			// Elder Maren's house: walk by and it opens up to show her asleep.
			const ARPGWorldBuilder::FHouse* House = B->HouseAt(Elder->GetActorLocation());
			if (!House) { Report(TEXT("FAIL: Maren isn't in a house")); Quit(0.5f); return; }
			Place(House->Door + FVector(0, 150, 100), 90.f);
			Step = 5; Next = T + 0.8f;
		}
		else if (Step == 5)
		{
			const ARPGWorldBuilder::FHouse* House = B->HouseAt(Elder->GetActorLocation());
			const bool bCut = B->IsCutawayCut(House->Cutaway);
			Report(FString::Printf(TEXT("%s: passing Maren's house at night shows her asleep inside (cut away: %s)"), bCut ? TEXT("PASS") : TEXT("FAIL"), bCut ? TEXT("yes") : TEXT("no")));
			Shot(TEXT("sleep_house"));
			ATSInteractable* Door = ATSInteractable::Nearest(GetWorld(), House->Door, 200.f);
			Report(FString::Printf(TEXT("%s: her door is there to use (%s)"), Door && S->IsLock(Door) ? TEXT("PASS") : TEXT("FAIL"), Door ? *Door->Id : TEXT("none")));
			if (Door) S->UseLock(Door, true);   // the Knight tries it (E)
			Step = 6; Next = T + 1.6f;
		}
		else if (Step == 6)
		{
			const ARPGWorldBuilder::FHouse* House = B->HouseAt(Elder->GetActorLocation());
			Report(FString::Printf(TEXT("%s: the Knight finds the door locked"), !B->IsCutawayOpen(House->Cutaway) ? TEXT("PASS") : TEXT("FAIL")));
			Pl->ApplyClass(TEXT("thief"), TEXT("male"));
			if (ATSInteractable* Door = ATSInteractable::Nearest(GetWorld(), House->Door, 200.f))
			{
				S->UseLock(Door, false);   // a click only tries it
				Report(FString::Printf(TEXT("%s: a click on the door doesn't open it, even for the Thief"), S->IsLocked(Door) && !Pl->Channel->IsActive() ? TEXT("PASS") : TEXT("FAIL")));
				S->UseLock(Door, true);    // E picks the lock
				Report(FString::Printf(TEXT("%s: E starts picking the lock (%s)"), Pl->Channel->IsActive() ? TEXT("PASS") : TEXT("FAIL"), *Pl->Channel->Label));
			}
			Step = 7; Next = T + 1.8f;
		}
		else if (Step == 7)
		{
			const ARPGWorldBuilder::FHouse* House = B->HouseAt(Elder->GetActorLocation());
			const bool bFromOutside = Elder->CanBeTargeted(Pl);
			Report(FString::Printf(TEXT("%s: the Thief picks the lock and the house opens (she still can't be reached from outside: %s)"),
				B->IsCutawayOpen(House->Cutaway) && !bFromOutside ? TEXT("PASS") : TEXT("FAIL"), bFromOutside ? TEXT("no") : TEXT("yes")));
			// Walk in through the door gap, to the foot of the bed.
			Pl->Control->TestClick(House->Bed + FVector(160.f, 40.f, -40.f));
			Step = 8; Next = T + 4.f;
		}
		else if (Step == 8)
		{
			const ARPGWorldBuilder::FHouse* House = B->HouseAt(Elder->GetActorLocation());
			const bool bInside = B->CutawayAt(Pl->GetActorLocation()) == House->Cutaway;
			Report(FString::Printf(TEXT("%s: the Thief walks in (inside: %s, at %s) and can reach her by the bed (%s); she sleeps on"),
				bInside && Elder->CanBeTargeted(Pl) && Asleep(Elder) ? TEXT("PASS") : TEXT("FAIL"), bInside ? TEXT("yes") : TEXT("no"), *Pl->GetActorLocation().ToCompactString(),
				Elder->CanBeTargeted(Pl) ? TEXT("yes") : TEXT("no")));
			Shot(TEXT("sleep_inside"));
			ATSSky::SetHour(7.5f);
			Step = 9; Next = T + 4.f;
		}
		else if (Step == 9)
		{
			ARPGEnemy* Brask = Find(TEXT("bandit_captain"));
			Report(FString::Printf(TEXT("%s: morning (%.1fh): Maren is up and out of the house (%s), Brask is up (%s)"),
				!Asleep(Elder) && !Indoors(Elder) && Brask && !Asleep(Brask) ? TEXT("PASS") : TEXT("FAIL"), ATSSky::Hour(),
				Indoors(Elder) ? TEXT("still in") : TEXT("out"), Brask && Asleep(Brask) ? TEXT("asleep") : TEXT("awake")));
			// Pictures of the day sleepers' beds (south of the river: last, as crossing turns the bandits hostile).
			if (ATSInteractable* Gate = ATSInteractable::Find(GetWorld(), TEXT("graveyard_gate"))) Place(Gate->GetActorLocation() + FVector(0, -250, 120), 90.f);
			Step = 11; Next = T + 6.f;   // the skeletons file back in
		}
		else if (Step == 11) { Shot(TEXT("sleep_graveyard")); Step = 12; Next = T + 0.5f; }
		else if (Step == 12)
		{
			if (ARPGEnemy* Brute = Find(TEXT("brute"))) Place(Brute->GetActorLocation() + FVector(-260, 160, 0), 0.f);
			Step = 13; Next = T + 1.5f;
		}
		else if (Step == 13)
		{
			ARPGEnemy* Brute = Find(TEXT("brute"));
			Report(FString::Printf(TEXT("%s: by day again the brute sleeps on its hay"), Brute && Asleep(Brute) ? TEXT("PASS") : TEXT("FAIL")));
			Shot(TEXT("sleep_hay"));
			Quit(1.f);
		}
	}
	else if (Scenario == TEXT("heist"))
	{
		// The Thief's whole plan (TODO.md T1-T6), run with -RPGHour=23: crouch (Space), quiet steps, lifting from behind,
		// getting caught, Brask's toll purse (the band scatters), Ossric's badge (the Bonewardens drift apart), a
		// villager robbed in her bed (E picks her lock), Tobin catching you (prices rise), the brute's relic lifted
		// while it sleeps by day, the robbed brute prowling the village at night, and the relic put back on its hay.
		ULMStory* L = S->Story();
		auto Asleep = [](const ATSCharacter* C) { return UTSSleep::IsAsleep(C); };
		auto Npc = [this](const FString& Id) -> ARPGNPC* { for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) if (It->NpcId == Id) return *It; return nullptr; };
		auto Behind = [this, &D](ATSCharacter* C, float Px) { Place(C->GetActorLocation() - C->Facing() * D.Px(Px) + FVector(0, 0, 60), C->GetActorRotation().Yaw); };
		auto ArcherAt = [this](int32 Skip) -> ARPGEnemy* { int32 N = 0; for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->Type == TEXT("archer") && !It->IsDead() && !It->IsLeaving() && N++ == Skip) return *It; return nullptr; };
		ARPGWorldBuilder* B = S->Builder();
		if (Step == 0)
		{
			Pl->GainXp(3000);   // level for the well-guarded prizes
			Report(FString::Printf(TEXT("thief at level %d, %.0f uu/s standing"), Pl->Level(), Pl->GetCharacterMovement()->MaxWalkSpeed));
			const float Standing = Pl->GetCharacterMovement()->MaxWalkSpeed;
			Pl->TestPress(TEXT("Dodge"), true); Pl->TestPress(TEXT("Dodge"), false);   // Space
			Report(FString::Printf(TEXT("%s: Space crouches the Thief (sneaking %d, no dash %d, %.0f -> %.0f uu/s)"),
				Pl->IsSneaking() && !Pl->IsDodging() && Pl->GetCharacterMovement()->MaxWalkSpeed < Standing * 0.6f ? TEXT("PASS") : TEXT("FAIL"),
				Pl->IsSneaking(), !Pl->IsDodging(), Standing, Pl->GetCharacterMovement()->MaxWalkSpeed));
			Step = 1; Next = T + 2.f;
		}
		else if (Step == 1)
		{
			// An archer up for the night, held still (frozen) so "behind" stays behind.
			ARPGEnemy* A = ArcherAt(0);
			if (!A) { Report(TEXT("FAIL: no archer")); Quit(0.5f); return; }
			A->Tags.Add(TEXT("Frozen"), 8.f);
			Target = A;
			Behind(A, 30.f);
			const bool bQuiet = !RPGTheft::Notices(A, Pl);
			Pl->SetSneaking(false);
			const bool bLoud = RPGTheft::Notices(A, Pl);
			Pl->SetSneaking(true);
			Report(FString::Printf(TEXT("%s: 30px behind an archer: crouched it doesn't hear you (%s), standing it does (%s)"), bQuiet && bLoud ? TEXT("PASS") : TEXT("FAIL"), bQuiet ? TEXT("quiet") : TEXT("heard"), bLoud ? TEXT("heard") : TEXT("quiet")));
			Report(FString::Printf(TEXT("%s: the archer is within reach, nothing stops the lift ('%s', %s)"), RPGTheft::Target(Pl) == A && RPGTheft::Why(Pl, A).IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *RPGTheft::Why(Pl, A), *RPGTheft::Hint(A)));
			Started = float(Pl->Inventory->Currency);
			Pl->TestPress(TEXT("Interact"), true); Pl->TestPress(TEXT("Interact"), false);   // E: action mode
			Report(FString::Printf(TEXT("%s: E turns on action mode; over the archer the cursor is turning gears ('%s')"),
				Pl->Control->IsTalkMode() && Pl->ActionIcon(A, nullptr) == TEXT("gear") ? TEXT("PASS") : TEXT("FAIL"), *Pl->ActionLabel(A, nullptr)));
			// ...and a click on it from further back: the Thief creeps the rest of the way, then lifts.
			Behind(A, 110.f);
			Pl->ActionClick(A);
			Step = 101; Next = T + 1.2f;
		}
		else if (Step == 101)
		{
			Report(FString::Printf(TEXT("%s: clicked from 110px back, the Thief creeps up and starts the lift (%s, %.0fuu away)"),
				Pl->Channel->IsActive() ? TEXT("PASS") : TEXT("FAIL"), *Pl->Channel->Label, Target.IsValid() ? FVector::Dist2D(Target->GetActorLocation(), Pl->GetActorLocation()) : -1.f));
			// Standing, the same archer would be a conversation (grey: it speaks Grave-speech); never a theft.
			Pl->SetSneaking(false);
			const FName Standing = Pl->ActionIcon(Target.Get(), nullptr);
			Pl->SetSneaking(true);
			Report(FString::Printf(TEXT("%s: standing, the cursor over it offers talk, not theft (%s)"), Standing == TEXT("talk") || Standing == TEXT("talk_off") ? TEXT("PASS") : TEXT("FAIL"), *Standing.ToString()));
			Step = 2; Next = T + 2.f;
		}
		else if (Step == 2)
		{
			ARPGEnemy* A = Cast<ARPGEnemy>(Target.Get());
			Report(FString::Printf(TEXT("%s: lifted unseen: +%d gold, pockets now empty (%s), the archer none the wiser (provoked %d)"),
				A && L->HasFlag(RPGTheft::PocketKey(A)) && Pl->Inventory->Currency > int32(Started) && !A->bProvoked ? TEXT("PASS") : TEXT("FAIL"),
				Pl->Inventory->Currency - int32(Started), A && L->HasFlag(RPGTheft::PocketKey(A)) ? TEXT("yes") : TEXT("no"), A ? A->bProvoked : -1));
			Shot(TEXT("heist_sneak"));
			// A second archer: it turns round mid-lift.
			ARPGEnemy* A2 = ArcherAt(1);
			if (!A2) { Report(TEXT("FAIL: no second archer")); Quit(0.5f); return; }
			A2->Tags.Add(TEXT("Frozen"), 8.f);
			Target = A2;
			Behind(A2, 30.f);
			MinDist = L->Mood;
			RPGTheft::Begin(Pl, A2);
			Step = 3; Next = T + 0.4f;
		}
		else if (Step == 3)
		{
			if (ARPGEnemy* A2 = Cast<ARPGEnemy>(Target.Get())) A2->SetActorRotation(FRotator(0, (Pl->GetActorLocation() - A2->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw, 0));
			Step = 4; Next = T + 0.4f;
		}
		else if (Step == 4)
		{
			ARPGEnemy* A2 = Cast<ARPGEnemy>(Target.Get());
			Report(FString::Printf(TEXT("%s: it turns round mid-lift: caught! (provoked %d, mood %.0f -> %.0f, lift stopped %d)"),
				A2 && A2->bProvoked && L->Mood < MinDist && !Pl->Channel->IsActive() ? TEXT("PASS") : TEXT("FAIL"), A2 ? A2->bProvoked : -1, MinDist, L->Mood, !Pl->Channel->IsActive()));
			// (Regression: a crouched action-mode click on a foe that's on to you is refused, and the moves after it
			// mustn't trip over the refused steal: this crashed in play on 2026-10-08.)
			if (A2)
			{
				A2->Tags.Remove(TEXT("Frozen"));
				Pl->Control->SetTalkMode(true);
				Pl->ActionClick(A2);
				Pl->Control->TestClick(Pl->GetActorLocation() + FVector(300, 0, 0));
				for (int32 I = 0; I < 5; ++I) Pl->Control->Update(0.05f);
				Report(FString::Printf(TEXT("%s: a crouched action click on a foe aiming at you is refused, and walking on afterwards is fine (stealing %d)"),
					!Pl->Channel->IsActive() && !Pl->IsStealing() ? TEXT("PASS") : TEXT("FAIL"), Pl->IsStealing()));
				Pl->Control->ClearGoal();
				A2->Die(nullptr);   // out of the way
			}
			// Brask's toll purse, at the sleeping camp.
			ARPGEnemy* Brask = Find(TEXT("bandit_captain"));
			if (!Brask || !Asleep(Brask)) { Report(FString::Printf(TEXT("FAIL: Brask %s"), Brask ? TEXT("awake") : TEXT("missing"))); Quit(0.5f); return; }
			Target = Brask;
			Place(Brask->GetActorLocation() + FVector(0, -D.Px(85), 60), 90.f);
			Report(FString::Printf(TEXT("%s: Brask sleeps; his purse is there for the taking (the cursor: %s)"), Pl->ActionIcon(Brask, nullptr) == TEXT("gear") ? TEXT("PASS") : TEXT("FAIL"), *Pl->ActionLabel(Brask, nullptr)));
			Pl->ActionClick(Brask);   // from a few steps off: creep up, then lift
			Step = 5; Next = T + 1.6f;
		}
		else if (Step == 5)
		{
			Report(FString::Printf(TEXT("%s: the Thief crept up to sleeping Brask and is lifting (%s)"), Pl->Channel->IsActive() ? TEXT("PASS") : TEXT("FAIL"), *Pl->Channel->Label));
			Shot(TEXT("heist_purse"));
			Step = 6; Next = T + 2.2f;
		}
		else if (Step == 6)
		{
			int32 Leaving = 0, Total = 0;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == TEXT("bandits")) { ++Total; Leaving += It->IsLeaving() ? 1 : 0; }
			Report(FString::Printf(TEXT("%s: the toll purse is gone: the bridge is resolved '%s', Brask is afraid (%d), the Red Hands scatter (%d / %d leaving)"),
				L->Flags.FindRef(TEXT("toll_outcome")) == TEXT("robbed") && L->HasFlag(TEXT("brask_fear")) && Leaving == Total && Total > 0 ? TEXT("PASS") : TEXT("FAIL"),
				*L->Flags.FindRef(TEXT("toll_outcome")), L->HasFlag(TEXT("brask_fear")), Leaving, Total));
			// Captain Ossric's badge (up for the night with his band).
			ARPGEnemy* Cap = Find(TEXT("skeleton_captain"));
			if (!Cap) { Report(TEXT("FAIL: no Ossric")); Quit(0.5f); return; }
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == TEXT("bonewardens")) It->Tags.Add(TEXT("Frozen"), 6.f);
			Target = Cap;
			Behind(Cap, 28.f);
			RPGTheft::Begin(Pl, Cap);
			Step = 7; Next = T + 2.2f;
		}
		else if (Step == 7)
		{
			int32 Leaving = 0, Total = 0;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == TEXT("bonewardens")) { ++Total; Leaving += It->IsLeaving() ? 1 : 0; }
			Report(FString::Printf(TEXT("%s: Ossric's badge lifted (have it %d): the Bonewardens drift apart (%d / %d leaving)"),
				Pl->Inventory->Count(TEXT("rank_badge")) > 0 && Leaving == Total && Total > 0 ? TEXT("PASS") : TEXT("FAIL"), Pl->Inventory->Count(TEXT("rank_badge")), Leaving, Total));
			// The graveyard gate is a lock like any other: the Thief picks it.
			if (ATSInteractable* Gate = ATSInteractable::Find(GetWorld(), TEXT("graveyard_gate")))
			{
				Report(FString::Printf(TEXT("%s: the graveyard gate is a lock, still shut, and the Thief's cursor turns gears at it (%s)"),
					S->IsLock(Gate) && S->IsLocked(Gate) && Pl->ActionIcon(nullptr, Gate) == TEXT("gear") ? TEXT("PASS") : TEXT("FAIL"), *Pl->ActionLabel(nullptr, Gate)));
				Place(Gate->GetActorLocation() + FVector(0, -120, 100), 90.f);
				S->UseLock(Gate, true);
			}
			Step = 70; Next = T + 3.5f;
		}
		else if (Step == 70)
		{
			ATSInteractable* Gate = ATSInteractable::Find(GetWorld(), TEXT("graveyard_gate"));
			Report(FString::Printf(TEXT("%s: after a long pick the graveyard gate opens"), Gate && !S->IsLocked(Gate) ? TEXT("PASS") : TEXT("FAIL")));
			Shot(TEXT("heist_gate"));
			Step = 71; Next = T + 0.3f;
		}
		else if (Step == 71)
		{
			// Maren, asleep in her cottage: pick the lock (E), walk in, lift her purse.
			ARPGNPC* Elder = Npc(TEXT("elder"));
			const ARPGWorldBuilder::FHouse* House = Elder ? B->HouseAt(Elder->GetActorLocation()) : nullptr;
			if (!House || !Asleep(Elder)) { Report(TEXT("FAIL: Maren isn't asleep at home")); Quit(0.5f); return; }
			Pl->SetSneaking(false);
			Place(House->Door + FVector(0, 60, 100), -90.f);
			if (ATSInteractable* Door = ATSInteractable::Nearest(GetWorld(), House->Door, 200.f)) S->UseLock(Door, true);
			Step = 8; Next = T + 2.f;
		}
		else if (Step == 8)
		{
			ARPGNPC* Elder = Npc(TEXT("elder"));
			const ARPGWorldBuilder::FHouse* House = B->HouseAt(Elder->GetActorLocation());
			Report(FString::Printf(TEXT("%s: Maren's door picked open"), B->IsCutawayOpen(House->Cutaway) ? TEXT("PASS") : TEXT("FAIL")));
			Pl->Control->TestClick(House->Bed + FVector(140.f, 60.f, -40.f));
			Step = 9; Next = T + 3.5f;
		}
		else if (Step == 9)
		{
			ARPGNPC* Elder = Npc(TEXT("elder"));
			Pl->SetSneaking(true);
			MinDist = L->Mood;
			Started = float(Pl->Inventory->Currency);
			Report(FString::Printf(TEXT("%s: by Maren's bed she can be robbed ('%s')"), RPGTheft::Target(Pl) == Elder && RPGTheft::Why(Pl, Elder).IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), *RPGTheft::Why(Pl, Elder)));
			Pl->SetSneaking(false);
			const FName Standing = Pl->ActionIcon(Elder, nullptr);
			Pl->SetSneaking(true);
			Report(FString::Printf(TEXT("%s: asleep: crouched the cursor steals (%s), standing it talks (%s)"),
				Pl->ActionIcon(Elder, nullptr) == TEXT("gear") && Standing == TEXT("talk") ? TEXT("PASS") : TEXT("FAIL"), *Pl->ActionIcon(Elder, nullptr).ToString(), *Standing.ToString()));
			Pl->ActionClick(Elder);
			Step = 10; Next = T + 0.6f;
		}
		else if (Step == 10) { Shot(TEXT("heist_house")); Step = 11; Next = T + 1.4f; }
		else if (Step == 11)
		{
			ARPGNPC* Elder = Npc(TEXT("elder"));
			Report(FString::Printf(TEXT("%s: Maren's purse lifted in her sleep (+%d gold, still asleep %d, mood %.0f -> %.0f: a small wrong)"),
				L->HasFlag(RPGTheft::PocketKey(Elder)) && Asleep(Elder) && L->Mood < MinDist && L->Mood > MinDist - 2.f ? TEXT("PASS") : TEXT("FAIL"),
				Pl->Inventory->Currency - int32(Started), Asleep(Elder), MinDist, L->Mood));
			// Tobin: woken mid-lift, he catches you.
			ARPGNPC* Tobin = Npc(TEXT("merchant"));
			const ARPGWorldBuilder::FHouse* House = Tobin ? B->HouseAt(Tobin->GetActorLocation()) : nullptr;
			if (!House) { Report(TEXT("FAIL: Tobin isn't at home")); Quit(0.5f); return; }
			B->SetCutawayOpen(House->Cutaway, true);
			Place(Tobin->GetActorLocation() + FVector(D.Px(16), 0, 20), 180.f);
			Started = float(S->PriceOf(100));
			MinDist = L->Mood;
			RPGTheft::Begin(Pl, Tobin);
			Step = 12; Next = T + 0.4f;
		}
		else if (Step == 12)
		{
			if (UTSSleep* Z = UTSSleep::Of(Npc(TEXT("merchant")))) Z->Wake();
			Step = 13; Next = T + 0.6f;
		}
		else if (Step == 13)
		{
			Report(FString::Printf(TEXT("%s: Tobin wakes mid-lift and catches you: word gets round (thief_known %d), prices %d -> %d, mood %.0f -> %.0f"),
				L->HasFlag(TEXT("thief_known")) && S->PriceOf(100) > int32(Started) && L->Mood <= MinDist - 3.f ? TEXT("PASS") : TEXT("FAIL"),
				L->HasFlag(TEXT("thief_known")), int32(Started), S->PriceOf(100), MinDist, L->Mood));
			// The brute's relic: by day, while it sleeps on its hay.
			ATSSky::SetHour(13.f);
			Place(S->GroundAt(44, 3) + FVector(0, 0, 120), 180.f);   // out of the way while the brute goes to bed
			Step = 14; Next = T + 8.f;
		}
		else if (Step == 14)
		{
			ARPGEnemy* Brute = Find(TEXT("brute"));
			if (!Brute) { Report(TEXT("FAIL: no brute")); Quit(0.5f); return; }
			Target = Brute;
			Place(Brute->GetActorLocation() + FVector(0, D.Px(26), 60), -90.f);
			Report(FString::Printf(TEXT("%s: by day the brute sleeps on its hay (%d) and the relic can be lifted ('%s')"), Asleep(Brute) && RPGTheft::Why(Pl, Brute).IsEmpty() ? TEXT("PASS") : TEXT("FAIL"), Asleep(Brute), *RPGTheft::Why(Pl, Brute)));
			RPGTheft::Begin(Pl, Brute);
			Step = 15; Next = T + 2.8f;
		}
		else if (Step == 15)
		{
			ARPGEnemy* Brute = Cast<ARPGEnemy>(Target.Get());
			Report(FString::Printf(TEXT("%s: the relic lifted from under the sleeping brute (have it %d, still asleep %d)"),
				Pl->Inventory->Count(TEXT("relic")) > 0 && L->HasFlag(TEXT("relic_stolen")) && Brute && Asleep(Brute) ? TEXT("PASS") : TEXT("FAIL"), Pl->Inventory->Count(TEXT("relic")), Brute && Asleep(Brute)));
			// Out of the mine, far off in the village's north-east; then night falls.
			Place(S->GroundAt(44, 3) + FVector(0, 0, 120), 180.f);
			ATSSky::SetHour(23.f);
			Step = 16; Next = T + 30.f;
		}
		else if (Step == 16)
		{
			ARPGEnemy* Brute = Cast<ARPGEnemy>(Target.Get());
			const bool bOut = Brute && !D.AreaAt(Brute->GetActorLocation());
			Report(FString::Printf(TEXT("%s: at night the robbed brute leaves the mine and prowls the village (prowling %d, out of the mine %d, at tile %.0f,%.0f)"),
				Brute && Brute->IsProwling() && bOut ? TEXT("PASS") : TEXT("FAIL"), Brute ? Brute->IsProwling() : -1, bOut,
				Brute ? Brute->GetActorLocation().X / D.TileSize : 0.f, Brute ? Brute->GetActorLocation().Y / D.TileSize : 0.f));
			if (Brute) Place(Brute->GetActorLocation() + FVector(0, 900, 300), -90.f);
			Step = 17; Next = T + 0.4f;
		}
		else if (Step == 17)
		{
			Shot(TEXT("heist_prowl"));
			// Put the relic back on its hay: the hunt ends.
			ATSInteractable* Hay = ATSInteractable::Find(GetWorld(), TEXT("hay_return"));
			Report(FString::Printf(TEXT("%s: the hay offers to take the relic back"), Hay && Hay->IsShown() && Hay->CanUse() ? TEXT("PASS") : TEXT("FAIL")));
			if (Hay) S->UseInteractable(Hay);
			const int32 Leave = L->FindChoice(TEXT("[Leave the relic"));
			if (Leave != INDEX_NONE) L->Choose(Leave);
			L->CloseDialogue();
			Step = 18; Next = T + 0.5f;
		}
		else if (Step == 18)
		{
			ARPGEnemy* Brute = Cast<ARPGEnemy>(Target.Get());
			Report(FString::Printf(TEXT("%s: the relic is back on its hay: the brute stops prowling (relic_returned %d, prowling %d)"),
				L->HasFlag(TEXT("relic_returned")) && Brute && !Brute->IsProwling() && Pl->Inventory->Count(TEXT("relic")) == 0 ? TEXT("PASS") : TEXT("FAIL"),
				L->HasFlag(TEXT("relic_returned")), Brute ? Brute->IsProwling() : -1));
			Report(FString::Printf(TEXT("world mood at the end: %.0f"), L->Mood));
			Quit(1.f);
		}
	}
	else if (Scenario == TEXT("lockpick"))
	{
		// A lock beyond the Thief's level: he can try, the bar gets a little over halfway, turns red and snaps. A level
		// later the same lock gives.
		ATSInteractable* Gate = ATSInteractable::Find(GetWorld(), TEXT("graveyard_gate"));
		if (!Gate) { Report(TEXT("FAIL: no graveyard gate")); Quit(0.5f); return; }
		if (Step == 0)
		{
			Place(Gate->GetActorLocation() + FVector(0, -120, 100), 90.f);
			Report(FString::Printf(TEXT("%s: a level-%d Thief still gets turning gears at the level-2 gate ('%s')"),
				Pl->ActionIcon(nullptr, Gate) == TEXT("gear") ? TEXT("PASS") : TEXT("FAIL"), Pl->Level(), *Pl->ActionLabel(nullptr, Gate)));
			S->UseLock(Gate, true);
			Report(FString::Printf(TEXT("%s: he starts picking it"), Pl->Channel->IsActive() ? TEXT("PASS") : TEXT("FAIL")));
			Step = 1; Next = T + 0.2f;
		}
		else if (Step == 1)
		{
			if (Pl->Channel->IsSnapped())
			{
				Report(FString::Printf(TEXT("%s: the pick snaps at %.0f%% (a little over halfway)"), Pl->Channel->SnappedAt() > 0.5f && Pl->Channel->SnappedAt() < 0.7f ? TEXT("PASS") : TEXT("FAIL"), Pl->Channel->SnappedAt() * 100.f));
				Step = 2; Next = T + 0.15f;
			}
			else if (!Pl->Channel->IsActive()) { Report(TEXT("FAIL: the attempt ended without snapping")); Quit(0.5f); }
			else Next = T + 0.05f;
		}
		else if (Step == 2) { Shot(TEXT("lockpick_snap")); Step = 3; Next = T + 1.2f; }
		else if (Step == 3)
		{
			Report(FString::Printf(TEXT("%s: the gate is still locked"), S->IsLocked(Gate) ? TEXT("PASS") : TEXT("FAIL")));
			while (Pl->Level() < 2) Pl->GainXp(ARPGPlayerCharacter::XpToNext(Pl, Pl->Level()) - Pl->Xp);
			S->UseLock(Gate, true);
			Step = 4; Next = T + 3.6f;
		}
		else if (Step == 4)
		{
			Report(FString::Printf(TEXT("%s: at level %d the same gate gives"), !S->IsLocked(Gate) ? TEXT("PASS") : TEXT("FAIL"), Pl->Level()));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("nighteyes"))
	{
		// Run with -RPGHour=23: the Thief's night eyes draw the dark beyond his sight as a dim grey; the Knight's is black.
		if (Step == 0) { Place(S->GroundAt(24, 7) + FVector(0, 0, 120), 90.f); Step = 1; Next = T + 2.f; return; }
		if (Step == 1)
		{
			Report(FString::Printf(TEXT("%s: the Thief's night shade is lighter (%.2f of the dark %.2f)"), ATSSky::ShadeStrength() < ATSSky::Darkness() * 0.7f && ATSSky::Darkness() > 0.5f ? TEXT("PASS") : TEXT("FAIL"), ATSSky::ShadeStrength(), ATSSky::Darkness()));
			Shot(TEXT("nighteyes_thief"));
			Step = 2; Next = T + 0.5f;
		}
		else if (Step == 2) { Pl->ApplyClass(TEXT("knight"), TEXT("male")); Step = 3; Next = T + 1.f; }
		else if (Step == 3)
		{
			Report(FString::Printf(TEXT("%s: the Knight's is full dark (%.2f)"), FMath::IsNearlyEqual(ATSSky::ShadeStrength(), ATSSky::Darkness()) ? TEXT("PASS") : TEXT("FAIL"), ATSSky::ShadeStrength()));
			Shot(TEXT("nighteyes_knight"));
			Quit(0.8f);
		}
	}
	else if (Scenario == TEXT("crouch"))
	{
		// The Thief's sneak look (run by day): the ninja-creep sheet and the shadow cloak while crouched, walking.
		if (Step == 0)
		{
			Place(S->GroundAt(20, 6) + FVector(0, 0, 120), 0.f);
			Pl->SetSneaking(true);
			Pl->Control->TestClick(S->GroundAt(26, 6));   // creep east (side view)
			Step = 1; Next = T + 1.2f;
		}
		else if (Step == 1) { Shot(TEXT("crouch_side")); Pl->Control->TestClick(S->GroundAt(24, 9)); Step = 2; Next = T + 0.9f; }
		else if (Step == 2) { Shot(TEXT("crouch_front")); Step = 20; Next = T + 0.3f; }   // (shots land a frame late: stand up after)
		else if (Step == 20) { Pl->SetSneaking(false); Pl->Control->TestClick(S->GroundAt(30, 9)); Step = 3; Next = T + 0.7f; }
		else if (Step == 3)
		{
			Shot(TEXT("crouch_standing"));
			FLinearColor Tint;
			Report(FString::Printf(TEXT("%s: the shadow cloak is a status tint for Sneaking"), ATSCharacter::TintForTag(Pl, TEXT("Sneaking"), Tint) ? TEXT("PASS") : TEXT("FAIL")));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("burrow"))
	{
		// The skeletons dig in and out of the graveyard by the bone-hole under its west wall; the Scholar learns the way
		// from Aldric once his grave is marked, goes in, reads the Bonewardens' headstones, and comes back out.
		ULMStory* L = S->Story();
		ARPGWorldBuilder* B = S->Builder();
		ATSInteractable* Hole = ATSInteractable::Find(GetWorld(), TEXT("graveyard_burrow"));
		ATSInteractable* HoleIn = ATSInteractable::Find(GetWorld(), TEXT("graveyard_burrow_in"));
		const int32* Yard = B ? B->Enclosures.Find(TEXT("graveyard")) : nullptr;
		if (!Hole || !HoleIn || !Yard) { Report(TEXT("FAIL: no bone-hole or graveyard")); Quit(0.5f); return; }
		auto Inside = [&]() { return B->CutawayAt(Pl->GetActorLocation()) == *Yard; };
		if (Step == 0)
		{
			ARPGEnemy* Sk = Find(TEXT("archer"));
			const UTSSleep* Z = Sk ? UTSSleep::Of(Sk) : nullptr;
			Report(FString::Printf(TEXT("%s: the skeletons go in and out by the bone-hole, not the gate (%.0fuu apart)"),
				Z && FVector::Dist2D(Z->Entry, Hole->GetActorLocation()) < 60.f ? TEXT("PASS") : TEXT("FAIL"), Z ? FVector::Dist2D(Z->Entry, Hole->GetActorLocation()) : -1.f));
			Place(Hole->GetActorLocation() + FVector(-200, 0, 100), 0.f);
			S->UseInteractable(Hole);
			Step = 1; Next = T + 1.5f;
		}
		else if (Step == 1)
		{
			Report(FString::Printf(TEXT("%s: not knowing the secret, it's just loose earth (still outside: %d)"), !Inside() ? TEXT("PASS") : TEXT("FAIL"), !Inside()));
			// Aldric's grave: marked; then, at night, he tells the Scholar the way in as he goes to rest.
			L->SetFlag(TEXT("grave_marked"));
			ATSSky::SetHour(23.f);
			Step = 2; Next = T + 12.f;
		}
		else if (Step == 2)
		{
			ARPGEnemy* Watcher = Find(TEXT("skeleton_watcher"));
			if (!Watcher) { Report(TEXT("FAIL: no Aldric")); Quit(0.5f); return; }
			S->OpenDialogue(Watcher);
			const int32 Done = L->FindChoice(TEXT("It's done"));
			if (Done != INDEX_NONE) L->Choose(Done);
			L->CloseDialogue();
			Report(FString::Printf(TEXT("%s: laid to rest, Aldric whispers the way in (graveyard_secret %d)"), L->HasFlag(TEXT("graveyard_secret")) ? TEXT("PASS") : TEXT("FAIL"), L->HasFlag(TEXT("graveyard_secret"))));
			Place(Hole->GetActorLocation() + FVector(-200, 0, 100), 0.f);
			Step = 3; Next = T + 0.6f;
		}
		else if (Step == 3) { S->UseInteractable(Hole); Step = 4; Next = T + 1.5f; }
		else if (Step == 4)
		{
			Report(FString::Printf(TEXT("%s: the Scholar slips in by the bone-hole (inside the graveyard: %d)"), Inside() ? TEXT("PASS") : TEXT("FAIL"), Inside()));
			Shot(TEXT("burrow_inside"));
			if (ATSInteractable* Stones = ATSInteractable::Find(GetWorld(), TEXT("bonewarden_stones")))
			{
				const int32 Xp0 = Pl->Xp, Lv0 = Pl->Level();
				S->UseInteractable(Stones);
				const int32 Read = L->FindChoice(TEXT("[Read the names"));
				if (Read != INDEX_NONE) L->Choose(Read);
				L->CloseDialogue();
				const bool bGained = Pl->Level() > Lv0 || Pl->Xp > Xp0;   // (a level-up resets the counter)
				Report(FString::Printf(TEXT("%s: the Bonewardens' headstones read, and the lore pays (xp %d -> %d, level %d -> %d)"),
					L->HasFlag(TEXT("read_stones")) && bGained ? TEXT("PASS") : TEXT("FAIL"), Xp0, Pl->Xp, Lv0, Pl->Level()));
			}
			Step = 5; Next = T + 0.5f;
		}
		else if (Step == 5) { S->UseInteractable(HoleIn); Step = 6; Next = T + 1.5f; }
		else if (Step == 6)
		{
			Report(FString::Printf(TEXT("%s: and back out the same way (outside: %d)"), !Inside() ? TEXT("PASS") : TEXT("FAIL"), !Inside()));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("shifts"))
	{
		// Who works when (run with -RPGHour=23): the troll lurks wide around the mine at night; Guard Brask (recruited)
		// stands his post all night and sleeps by day; Grot the Miner sleeps at the mine at night and works by day.
		ULMStory* L = S->Story();
		auto Npc = [this](const FString& Id) -> ARPGNPC* { for (TActorIterator<ARPGNPC> It(GetWorld()); It; ++It) if (It->NpcId == Id) return *It; return nullptr; };
		auto Indoors = [](const AActor* A) { const UTSSleep* Z = UTSSleep::Of(A); return Z && Z->IsIndoors(); };
		ARPGEnemy* Brute = Find(TEXT("brute"));
		if (!Brute) { Report(TEXT("FAIL: no brute")); Quit(0.5f); return; }
		if (Step == 0) { Place(S->GroundAt(44, 3) + FVector(0, 0, 120), 180.f); MinDist = 0.f; Swings = 0; Step = 1; Next = T + 2.f; return; }
		if (Step == 1)
		{
			// Watch the troll roam for a while (it lurks well away from its post at night).
			MinDist = FMath::Max(MinDist, float(FVector::Dist2D(Brute->GetActorLocation(), Brute->Home)));
			if (++Swings < 30) { Next = T + 0.5f; return; }
			Report(FString::Printf(TEXT("%s: at night the troll lurks around the mine (up to %.0fpx from its post; awake %d)"),
				MinDist > D.Px(110) && !UTSSleep::IsAsleep(Brute) ? TEXT("PASS") : TEXT("FAIL"), MinDist / D.Px(1), !UTSSleep::IsAsleep(Brute)));
			// Brask recruited as the town guard, and the troll taught a trade (becoming Grot).
			TArray<TSharedPtr<FJsonValue>> Acts;
			const TSJson::FObj R = MakeShared<FJsonObject>(); R->SetStringField(TEXT("recruit"), TEXT("brask_guard"));
			Acts.Add(MakeShared<FJsonValueObject>(R));
			L->RunActions(Acts);
			Brute->BecomeTrader();
			Step = 2; Next = T + 8.f;
		}
		else if (Step == 2)
		{
			ARPGNPC* Guard = Npc(TEXT("brask_guard"));
			Report(FString::Printf(TEXT("%s: at night Guard Brask is up at his post (asleep %d, indoors %d)"),
				Guard && !UTSSleep::IsAsleep(Guard) && !Indoors(Guard) ? TEXT("PASS") : TEXT("FAIL"), Guard ? UTSSleep::IsAsleep(Guard) : -1, Guard ? Indoors(Guard) : -1));
			Report(FString::Printf(TEXT("%s: at night Grot the Miner sleeps at the mine (asleep %d, carrying %d)"),
				UTSSleep::IsAsleep(Brute) && !Brute->IsCarrying() ? TEXT("PASS") : TEXT("FAIL"), UTSSleep::IsAsleep(Brute), Brute->IsCarrying()));
			ATSSky::SetHour(8.f);
			Step = 3; Next = T + 22.f;
		}
		else if (Step == 3)
		{
			ARPGNPC* Guard = Npc(TEXT("brask_guard"));
			Report(FString::Printf(TEXT("%s: by day Guard Brask sleeps in his cottage (asleep %d, indoors %d)"),
				Guard && UTSSleep::IsAsleep(Guard) && Indoors(Guard) ? TEXT("PASS") : TEXT("FAIL"), Guard ? UTSSleep::IsAsleep(Guard) : -1, Guard ? Indoors(Guard) : -1));
			const UTSRoutine* Rt = Brute->GetRoutine();
			Report(FString::Printf(TEXT("%s: by day Grot is up and on his rounds (asleep %d, heading to %s)"),
				!UTSSleep::IsAsleep(Brute) && Rt && Rt->CurrentTag() == TEXT("village") ? TEXT("PASS") : TEXT("FAIL"), UTSSleep::IsAsleep(Brute), Rt ? *Rt->CurrentTag().ToString() : TEXT("-")));
			Quit(0.5f);
		}
	}
	else if (Scenario == TEXT("night"))
	{
		// Run with -RPGHour=23: the mage's staff orb glows faintly and widens what the dark lets the hero see.
		if (Step == 0) { Step = 1; Next = T + 1.5f; return; }   // let the sky settle
		const float Base = float(TSJson::Num(TSJson::Obj(TSJson::Obj(D.World(), TEXT("dayNight")), TEXT("nightVision")), TEXT("heroSight"), 1000));
		float Orb = 0.f;
		TArray<UPointLightComponent*> Lights;
		Pl->GetComponents(Lights);
		for (const UPointLightComponent* L : Lights) if (L->GetName().StartsWith(TEXT("OrbLight"))) Orb = FMath::Max(Orb, L->Intensity);
		Report(FString::Printf(TEXT("%s: at night (%.1fh, night %.2f) the staff orb glows (%.1f cd)"), Orb > 1.f ? TEXT("PASS") : TEXT("FAIL"), ATSSky::Hour(), ATSSky::Night(), Orb));
		Report(FString::Printf(TEXT("%s: the hero sees further in the dark (%.0fuu, %.0fuu without the orb)"), ATSSky::HeroSight() > Base + 100.f ? TEXT("PASS") : TEXT("FAIL"), ATSSky::HeroSight(), Base));
		const UTSDayNight* DN = UTSDayNight::Get(this);
		Report(FString::Printf(TEXT("%s: Tessera announced the night (phase %d, hour %d, level %.2f)"), DN && DN->Phase() == ETSDayPhase::Night ? TEXT("PASS") : TEXT("FAIL"),
			DN ? int32(DN->Phase()) : -1, DN ? DN->Hour() : -1, DN ? DN->NightLevel() : -1.f));
		Shot(TEXT("night_orb"));
		Quit(1.f);
	}
	else if (Scenario == TEXT("barrier"))
	{
		// The mage's barrier: a slime already inside when it goes up is thrown clear; then, chasing, it is held at the edge.
		const float KeepOut = D.Px(TSJson::Num(TSJson::Obj(Pl->Style(), TEXT("secondary")), TEXT("keepOut"), 0));
		auto Edge = [&]() { return Target.IsValid() ? FVector::Dist2D(Target->GetActorLocation(), Pl->GetActorLocation()) - Target->Radius() : 0.f; };
		if (Step == 0)
		{
			SetTickGroup(TG_PostUpdateWork);   // measure what's drawn: after all movement and the barrier's push
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(Target->Radius() + 70.f, 0, 0), 0.f);   // right up against it
			AimAt(Target->GetActorLocation());
			Step = 1; Next = T + 0.3f;
		}
		else if (Step == 1)
		{
			Report(FString::Printf(TEXT("slime %.0fuu away (edge to edge); barrier up (keep-out %.0fuu)"), Edge(), KeepOut));
			Pl->TestPress(TEXT("Secondary"), true);
			Step = 2; Next = T + 1.0f;
		}
		else if (Step == 2)
		{
			Report(FString::Printf(TEXT("%s: the slime inside was thrown clear (now %.0fuu, barrier %.0fuu)"), Edge() >= KeepOut - 2.f ? TEXT("PASS") : TEXT("FAIL"), Edge(), KeepOut));
			MinDist = BIG_NUMBER;
			Started = T;
			Step = 3; Next = T + 0.05f;
		}
		else if (Step == 3)
		{
			if (Target.IsValid() && !Target->IsDead())
			{
				Target->State = ERPGEnemyState::Chase;   // keep it coming at the mage
				MinDist = FMath::Min(MinDist, Edge());
			}
			if (T - Started > 1.2f && !bShotTaken) { Shot(TEXT("barrier")); bShotTaken = true; }
			if (T - Started < 3.f) { Next = T + 0.05f; return; }
			Pl->TestPress(TEXT("Secondary"), false);
			Report(FString::Printf(TEXT("%s: chasing, it pressed in but stayed outside (closest %.0fuu, barrier %.0fuu)"),
				MinDist >= KeepOut - 2.f && MinDist < KeepOut + 80.f ? TEXT("PASS") : TEXT("FAIL"), MinDist, KeepOut));
			Quit(0.5f);
		}
	}
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
			Shot(TEXT("elder"));
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
			Report(FString::Printf(TEXT("mage vs archer: mana %.0f/%.0f, HP %.0f"), Pl->Stats->Pool(RPGStat::Mana).Current, Pl->Stats->Max(RPGStat::Mana), Pl->Stats->Health()));
			Step = 1; Next = T + 0.5f;
		}
		else if (Step == 1)
		{
			if (!Target.IsValid() || Target->IsDead()) { Report(FString::Printf(TEXT("archer dead. mana %.0f, HP %.0f/%.0f"), Pl->Stats->Pool(RPGStat::Mana).Current, Pl->Stats->Health(), Pl->Stats->MaxHealth())); Quit(2.f); return; }
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Attack"), true);
			Pl->TestPress(TEXT("Attack"), false);
			if (FMath::Fmod(T, 2.f) < 0.5f) Report(FString::Printf(TEXT("archer HP %.0f (%s), mana %.0f, HP %.0f"), Target->Stats->Health(), Target->State == ERPGEnemyState::Windup ? TEXT("winding up") : TEXT("-"), Pl->Stats->Pool(RPGStat::Mana).Current, Pl->Stats->Health()));
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
			Shot(Sh.Name);
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
			const float Before = Target->Stats->Health();
			AimAt(Target->GetActorLocation());
			Pl->TestPress(TEXT("Secondary"), false);  // release at full draw
			Report(FString::Printf(TEXT("released full draw at slime (HP %.0f)"), Before));
			Step = 2; Next = T + 1.5f;
		}
		else if (Step == 2)
		{
			Report(FString::Printf(TEXT("slime HP after arrow: %s"), Target.IsValid() && !Target->IsDead() ? *FString::Printf(TEXT("%.0f"), Target->Stats->Health()) : TEXT("dead")));
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
			for (const FTSFloater& F : UTSFeedback::Get(S)->Floaters) if (F.Text == TEXT("BACKSTAB")) ++Backstabs;
			Report(FString::Printf(TEXT("dagger from behind: backstab floaters=%d, slime %s"), Backstabs, Target->IsDead() ? TEXT("dead") : *FString::Printf(TEXT("HP %.0f"), Target->Stats->Health())));
			Quit(1.f);
		}
	}
}
