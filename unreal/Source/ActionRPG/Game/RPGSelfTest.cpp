#include "RPGSelfTest.h"
#include "ActionRPG.h"
#include "RPGData.h"
#include "RPGStory.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"
#include "RPGLoot.h"
#include "RPGInventoryComponent.h"

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

ARPGSelfTest::ARPGSelfTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;   // keeps driving dialogue while the game is paused
}

ARPGPlayerCharacter* ARPGSelfTest::P() const { return URPGStory::Get(this)->Player(); }

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
	URPGStory* S = URPGStory::Get(this);
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
			S->StartQuest(TEXT("slime_cull"));
			Target = Find(TEXT("slime"));
			Place(Target->GetActorLocation() - FVector(170, 0, 0), 0);
			Report(FString::Printf(TEXT("knight vs slime: slime HP %.0f, player HP %.0f"), Target->Stats->HP, Pl->Stats->HP));
			Step = 1; Next = T + 0.6f;
		}
		else if (Step == 1)
		{
			if (!Target.IsValid() || Target->IsDead())
			{
				Report(FString::Printf(TEXT("slime dead after %d swings. XP %d, level %d, quest %s, stamina %.0f"), Swings, Pl->Xp, Pl->Level(), *S->ProgressText(TEXT("slime_cull")), Pl->Stats->Stamina));
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
			Report(TEXT("walked up to the bridge"));
			Step = 1; Next = T + 1.5f;
		}
		else if (Step == 1)
		{
			if (!S->IsDialogueOpen()) { Report(TEXT("FAIL: Brask did not stop us")); Quit(0.5f); return; }
			FString Opts;
			for (const FRPGChoiceView& V : S->ChoiceViews) Opts += TEXT("\n      ") + (V.Verb.IsEmpty() ? FString() : TEXT("[") + V.Verb + TEXT("] ")) + V.Text;
			Report(TEXT("parley: ") + S->DialogueSpeaker + TEXT(": ") + S->DialogueText + Opts);
			Step = 2; Next = T + 2.5f;   // leave the dialogue on screen for a screenshot
		}
		else if (Step == 2)
		{
			const int32 I = S->FindChoice(TEXT("I am a knight"));
			if (I == INDEX_NONE) { Report(TEXT("FAIL: no honor option")); Quit(0.5f); return; }
			S->ForcedCheck = true;
			S->Choose(I);
			Report(TEXT("honor check -> ") + S->DialogueText);
			const int32 R = S->FindChoice(TEXT("(Raise"));
			if (R != INDEX_NONE) S->Choose(R);
			Report(FString::Printf(TEXT("duel started: %s. Others passive: %s"), S->Duel.IsValid() ? TEXT("yes") : TEXT("no"), Find(TEXT("bandit")) && Find(TEXT("bandit"))->IsPassive() ? TEXT("yes") : TEXT("no")));
			Pl->Stats->Base.FindOrAdd(TEXT("might")) += 25.f;   // keep the test short
			Step = 3; Next = T + 0.5f;
		}
		else if (Step == 3)
		{
			if (S->IsDialogueOpen())
			{
				Report(FString::Printf(TEXT("Brask yields at %.0f/%.0f HP after %d swings: %s"), Brask->Stats->HP, Brask->Stats->MaxHP(), Swings, *S->DialogueText.Left(60)));
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
			Report(FString::Printf(TEXT("after pressing 1: dialogue open=%d, paused=%d"), S->IsDialogueOpen(), GetWorld()->IsPaused()));
			if (S->IsDialogueOpen()) { Report(TEXT("FAIL: pressing 1 did not answer the dialogue")); Quit(0.5f); return; }
			Step = 4; Next = T + 3.f;
		}
		else if (Step == 4)
		{
			Report(FString::Printf(TEXT("after the dialogue: still auto-attacking=%d"), Pl->IsAttacking()));
			int32 Leaving = 0;
			for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (It->FactionId() == TEXT("bandits") && (It->IsLeaving() || It->IsDead())) ++Leaving;
			const FString* Outcome = S->Flags.Find(TEXT("toll_outcome"));
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
			Place(Target->GetActorLocation() - FVector(150, 0, 0), 0);
			AimAt(Target->GetActorLocation());
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
			Report(FString::Printf(TEXT("after the slime's attacks: HP %d -> %.0f (unblocked hit is ~7), stamina %.0f, guarding=%d"), Swings, Pl->Stats->HP, Pl->Stats->Stamina, Pl->IsGuarding()));
			Pl->TestPress(TEXT("Secondary"), false);
			Quit(1.f);
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
			for (const FRPGChoiceView& V : S->ChoiceViews) Opts += TEXT("\n      ") + (V.Verb.IsEmpty() ? FString() : TEXT("[") + V.Verb + TEXT("] ")) + V.Text;
			Report(FString::Printf(TEXT("dialogue open=%d: %s: %s%s"), S->IsDialogueOpen(), *S->DialogueSpeaker, *S->DialogueText, *Opts));
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
			S->Choose(S->FindChoice(TEXT("What's troubling")));
			Report(TEXT("asked about trouble -> ") + S->DialogueText.Left(80));
			const int32 Accept = S->ChoiceViews.IndexOfByPredicate([](const FRPGChoiceView& V) { return V.Text.StartsWith(TEXT("It shall be done")); });
			S->Choose(Accept);
			Report(FString::Printf(TEXT("quest toll_bridge: %s, dialogue open=%d"), *S->QuestStatus(TEXT("toll_bridge")), S->IsDialogueOpen()));
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
