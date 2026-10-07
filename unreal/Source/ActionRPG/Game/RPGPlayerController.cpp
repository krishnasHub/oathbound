#include "RPGPlayerController.h"
#include "TSFeedback.h"
#include "ActionRPG.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGPlayerCharacter.h"
#include "SRPGWidgets.h"
#include "STSDialogueBox.h"
#include "STSMenus.h"
#include "STSWidgets.h"
#include "TSCameraRig.h"
#include "TSData.h"
#include "TSLook.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/CommandLine.h"
#include "Widgets/SWeakWidget.h"

ARPGPlayerController::ARPGPlayerController()
{
	bShowMouseCursor = false;
}

void ARPGPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!GEngine || !GEngine->GameViewport) return;
	TWeakObjectPtr<UWorld> W = GetWorld();

	SAssignNew(Hud, SRPGHud).World(W);
	SAssignNew(Dialogue, STSDialogueBox).World(W).View(URPGSession::Get(this)->DialogueView());
	SAssignNew(Panel, SRPGPanel).World(W).OnClose(SRPGPanel::FOnClose::CreateUObject(this, &ARPGPlayerController::ClosePanel));
	GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	GEngine->GameViewport->AddViewportWidgetContent(Dialogue.ToSharedRef(), 20);
	GEngine->GameViewport->AddViewportWidgetContent(Panel.ToSharedRef(), 30);
	SAssignNew(PauseMenu, STSPauseMenu).World(W)
		.OnResume(FSimpleDelegate::CreateUObject(this, &ARPGPlayerController::ResumeGame))
		.OnNewGame(FSimpleDelegate::CreateUObject(this, &ARPGPlayerController::NewGame))
		.OnQuit(FSimpleDelegate::CreateUObject(this, &ARPGPlayerController::QuitGame));
	GEngine->GameViewport->AddViewportWidgetContent(PauseMenu.ToSharedRef(), 50);
	// The game's own cursor: what a click would do now (ARPGPlayerCharacter::CursorIcon), only while playing.
	SAssignNew(Cursor, STSCursor).IconFolder(TEXT("/Game/RPG/Pixel")).IconPrefix(TEXT("CUR_"))
		.Icons({ TEXT("pointer"), TEXT("sword"), TEXT("dagger"), TEXT("wand"), TEXT("arrow"), TEXT("talk"), TEXT("talk_off") })
		.Centred({ TEXT("talk"), TEXT("talk_off") })
		.IconFn([this]() { const ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn()); return P && IsInGameplay() ? P->CursorIcon() : FName(NAME_None); });
	GEngine->GameViewport->AddViewportWidgetContent(Cursor.ToSharedRef(), 100);   // on top of everything
	Dialogue->SetVisibility(EVisibility::Collapsed);
	Panel->SetVisibility(EVisibility::Collapsed);
	PauseMenu->SetVisibility(EVisibility::Collapsed);

	if (URPGSession* S = URPGSession::Get(this)) S->Story()->OnDialogueChanged.AddUObject(this, &ARPGPlayerController::OnDialogueChanged);
	ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn());
	if (P) P->BindUIHooks([this](FName K) { HandleKey(K); });

	// Automated runs: ignore the real keyboard/mouse so someone typing elsewhere can't steer the test.
	bNoInput = FParse::Param(FCommandLine::Get(), TEXT("RPGNoInput"));
	if (P) P->SetInputLocked(bNoInput);

	// Boot: the title screen. New Game from the pause menu reopens the level with ?RPGNewGame and goes straight
	// to character select; -RPGClass= (automated runs) skips both.
	FString Class;
	const AGameModeBase* GM = GetWorld()->GetAuthGameMode();
	const bool bNewGame = GM && UGameplayStatics::HasOption(GM->OptionsString, TEXT("RPGNewGame"));
	if (!bNewGame && FParse::Value(FCommandLine::Get(), TEXT("RPGClass="), Class)) EnterGameplay();
	else if (bNewGame) ShowCharSelect();
	else ShowTitle();
	// Automated runs: -RPGAutoSelect[=class] goes to character select at 2 s and stays (screenshots);
	// -RPGAutoBegin walks title -> character select (at 2 s) -> Begin (at 6 s).
	if (bTitle && (FParse::Param(FCommandLine::Get(), TEXT("RPGAutoSelect")) || FCommandLine::Get() && FCString::Strifind(FCommandLine::Get(), TEXT("RPGAutoSelect="))))
	{
		FString Show;
		FParse::Value(FCommandLine::Get(), TEXT("RPGAutoSelect="), Show);
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this, Show]() { if (bTitle) ShowCharSelect(); if (CharSelect && !Show.IsEmpty()) CharSelect->ShowClass(Show); }, 2.f, false);
	}
	if ((bTitle || bCharSelect) && FParse::Param(FCommandLine::Get(), TEXT("RPGAutoBegin")))
	{
		FTimerHandle H1, H2;
		GetWorldTimerManager().SetTimer(H1, [this]() { if (bTitle) ShowCharSelect(); }, 2.f, false);
		GetWorldTimerManager().SetTimer(H2, [this]() { if (bCharSelect) BeginGame(TEXT("knight"), TEXT("male")); }, 6.f, false);
	}
}

void ARPGPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GEngine && GEngine->GameViewport)
	{
		for (TSharedPtr<SWidget> Wd : TArray<TSharedPtr<SWidget>>{ Hud, Dialogue, CharSelect, Panel, PauseMenu, Title, Cursor })
			if (Wd) GEngine->GameViewport->RemoveViewportWidgetContent(Wd.ToSharedRef());
	}
	Super::EndPlay(Reason);
}

void ARPGPlayerController::EnterGameplay()
{
	if (UTSCameraRig::IsTopDown(this))
	{
		// Top-down: the cursor stays visible (it aims) and is kept inside the window.
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		SetInputMode(Mode);
		bShowMouseCursor = true;
		CurrentMouseCursor = DefaultMouseCursor = EMouseCursor::None;   // the game draws its own (STSCursor)
		return;
	}
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}

void ARPGPlayerController::EnterUI(TSharedPtr<SWidget> FocusWidget)
{
	// Modal UI: clicks can't reach the (paused) game behind it, keys go to the widget.
	FInputModeUIOnly Mode;
	if (FocusWidget) Mode.SetWidgetToFocus(FocusWidget);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	CurrentMouseCursor = DefaultMouseCursor = EMouseCursor::Default;   // menus use the normal pointer
	// The game never sees button releases while UI is up, so drop anything held (attack, block, bow, movement).
	if (ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn())) P->ClearHeldInput();
}

// ---------------------------------------------------------------------------------------------
// Character select
// ---------------------------------------------------------------------------------------------

void ARPGPlayerController::SetHeroHidden(bool bHide)
{
	if (APawn* P = GetPawn()) P->SetActorHiddenInGame(bHide);
}

void ARPGPlayerController::ShowTitle()
{
	bTitle = true;
	SetHeroHidden(true);
	Hud->SetVisibility(EVisibility::Collapsed);

	// A slow drift over the village at the game's own angle (and tilt-shift), behind the title.
	TitleFrom = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
	TitleT = 0.f;
	PreviewCam = GetWorld()->SpawnActor<ACameraActor>(TitleFrom, TSLook::CameraRotation());
	UCameraComponent* Cam = PreviewCam->GetCameraComponent();
	Cam->bConstrainAspectRatio = false;
	if (const ARPGPlayerCharacter* PC = Cast<ARPGPlayerCharacter>(GetPawn()))
	{
		Cam->SetFieldOfView(PC->Camera->FieldOfView);
		Cam->PostProcessSettings = PC->Camera->PostProcessSettings;
		Cam->PostProcessBlendWeight = 1.f;
	}
	bAutoManageActiveCameraTarget = false;
	SetViewTarget(PreviewCam);

	const TSJson::FObj TitleData = UTSData::Get(this).Section(TEXT("title"));
	SAssignNew(Title, STSTitle).World(GetWorld())
		.Title(TSJson::Str(TitleData, TEXT("name"), TEXT("Action RPG")))
		.Tagline(TSJson::Str(TitleData, TEXT("tagline")))
		.Footer(TSJson::Str(TitleData, TEXT("footer")))
		.OnStart(FSimpleDelegate::CreateLambda([this]() { if (!bNoInput) ShowCharSelect(); }))
		.OnQuit(FSimpleDelegate::CreateUObject(this, &ARPGPlayerController::QuitGame));
	GEngine->GameViewport->AddViewportWidgetContent(Title.ToSharedRef(), 45);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Title);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	CurrentMouseCursor = DefaultMouseCursor = EMouseCursor::Default;
}

void ARPGPlayerController::HideTitle()
{
	if (!Title) return;
	GEngine->GameViewport->RemoveViewportWidgetContent(Title.ToSharedRef());
	Title.Reset();
	bTitle = false;
}

void ARPGPlayerController::Tick(float Dt)
{
	Super::Tick(Dt);
	// Title drift: a slow sweep east over the village and back.
	if (bTitle && PreviewCam)
	{
		TitleT += Dt;
		const float Arm = 4300.f;
		const FVector Pan(FMath::Sin(TitleT * 0.05f) * 1800.f + 700.f, FMath::Sin(TitleT * 0.031f) * 300.f, 0.f);
		PreviewCam->SetActorLocation(TitleFrom + Pan - TSLook::CameraRotation().Vector() * Arm);
	}
}

void ARPGPlayerController::ShowCharSelect()
{
	if (!GetPawn()) return;
	HideTitle();
	bCharSelect = true;
	SetHeroHidden(true);
	if (!PreviewCam)   // straight here (New Game): the select screen is opaque, so any camera will do
	{
		bAutoManageActiveCameraTarget = true;
		SetViewTarget(GetPawn());
	}

	SAssignNew(CharSelect, SRPGCharSelect).World(GetWorld())
		.OnBegin(SRPGCharSelect::FOnBegin::CreateLambda([this](const FString& C, const FString& S) { if (!bNoInput) BeginGame(C, S); }))
		.OnPreview(SRPGCharSelect::FOnPreview::CreateLambda([this](const FString& C, const FString& S)
		{
			if (ARPGPlayerCharacter* PP = Cast<ARPGPlayerCharacter>(GetPawn())) PP->ApplyClass(C, S);
		}))
		.OnBack(FSimpleDelegate::CreateLambda([this]()
		{
			if (bNoInput) return;
			// Back to the title.
			GEngine->GameViewport->RemoveViewportWidgetContent(CharSelect.ToSharedRef());
			CharSelect.Reset();
			bCharSelect = false;
			if (PreviewCam) { PreviewCam->Destroy(); PreviewCam = nullptr; }
			ShowTitle();
		}));
	GEngine->GameViewport->AddViewportWidgetContent(CharSelect.ToSharedRef(), 40);
	Hud->SetVisibility(EVisibility::Collapsed);

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(CharSelect);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	CurrentMouseCursor = DefaultMouseCursor = EMouseCursor::Default;
}

void ARPGPlayerController::BeginGame(const FString& ClassId, const FString& Sex)
{
	if (ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn()))
	{
		P->ApplyClass(ClassId, Sex);
		SetControlRotation(P->GetActorRotation());
	}
	bCharSelect = false;
	HideTitle();
	if (CharSelect) GEngine->GameViewport->RemoveViewportWidgetContent(CharSelect.ToSharedRef());
	CharSelect.Reset();
	if (PreviewCam) { PreviewCam->Destroy(); PreviewCam = nullptr; }
	SetHeroHidden(false);
	bAutoManageActiveCameraTarget = true;
	SetViewTarget(GetPawn());
	Hud->SetVisibility(EVisibility::SelfHitTestInvisible);
	EnterGameplay();

	URPGSession* S = URPGSession::Get(this);
	UTSFeedback::Get(S)->Toast(FString::Printf(TEXT("%s %s — talk to Elder Maren to begin."), Sex == TEXT("female") ? TEXT("Female") : TEXT("Male"),
		*Cast<ARPGPlayerCharacter>(GetPawn())->DisplayName), Cast<ARPGPlayerCharacter>(GetPawn())->NameColor);
	UTSFeedback::Get(S)->Toast(TEXT("Press H for controls"));
}

// ---------------------------------------------------------------------------------------------
// Dialogue & panels
// ---------------------------------------------------------------------------------------------

void ARPGPlayerController::OnDialogueChanged()
{
	URPGSession* S = URPGSession::Get(this);
	if (!S || !Dialogue) return;
	if (S->Story()->IsDialogueOpen())
	{
		Dialogue->Refresh();
		Dialogue->SetVisibility(EVisibility::Visible);
		EnterUI(Dialogue);
		FSlateApplication::Get().SetKeyboardFocus(Dialogue);
	}
	else Dialogue->SetVisibility(EVisibility::Collapsed);
}

void ARPGPlayerController::HandleKey(FName Key)
{
	if (bCharSelect || bTitle) return;
	if (Key == TEXT("Escape"))
	{
		if (bPanelOpen) ClosePanel();
		else if (!bPauseMenu && !URPGSession::Get(this)->Story()->IsDialogueOpen()) OpenPauseMenu();
		return;
	}
	if (Key == TEXT("Inventory") || Key == TEXT("Character") || Key == TEXT("Quests") || Key == TEXT("Help")) TogglePanel(Key);
}

void ARPGPlayerController::TogglePanel(FName Mode)
{
	if (bPanelOpen && Panel->Mode == Mode) { ClosePanel(); return; }
	bPanelOpen = true;
	Panel->Show(Mode);
	Panel->SetVisibility(EVisibility::Visible);
	UGameplayStatics::SetGamePaused(this, true);
	EnterUI(Panel);
	FSlateApplication::Get().SetKeyboardFocus(Panel);
}

void ARPGPlayerController::OpenPauseMenu()
{
	if (bPauseMenu || bCharSelect || bTitle) return;
	bPauseMenu = true;
	PauseMenu->Open();
	PauseMenu->SetVisibility(EVisibility::Visible);
	UGameplayStatics::SetGamePaused(this, true);
	EnterUI(PauseMenu);
	FSlateApplication::Get().SetKeyboardFocus(PauseMenu);
}

void ARPGPlayerController::ResumeGame()
{
	if (!bPauseMenu) return;
	bPauseMenu = false;
	PauseMenu->SetVisibility(EVisibility::Collapsed);
	UGameplayStatics::SetGamePaused(this, false);
	EnterGameplay();
}

void ARPGPlayerController::NewGame()
{
	// No saves yet: a new game is a fresh world (story, quests, enemies all reset), starting at character select.
	UGameplayStatics::SetGamePaused(this, false);
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("RPGNewGame"));
}

void ARPGPlayerController::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ARPGPlayerController::ClosePanel()
{
	bPanelOpen = false;
	Panel->SetVisibility(EVisibility::Collapsed);
	UGameplayStatics::SetGamePaused(this, false);
	EnterGameplay();
}
