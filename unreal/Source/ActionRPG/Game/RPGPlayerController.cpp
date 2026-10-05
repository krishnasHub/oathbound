#include "RPGPlayerController.h"
#include "ActionRPG.h"
#include "RPGStory.h"
#include "RPGPlayerCharacter.h"
#include "SRPGWidgets.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
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
	SAssignNew(Dialogue, SRPGDialogue).World(W);
	SAssignNew(Panel, SRPGPanel).World(W).OnClose(SRPGPanel::FOnClose::CreateUObject(this, &ARPGPlayerController::ClosePanel));
	GEngine->GameViewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	GEngine->GameViewport->AddViewportWidgetContent(Dialogue.ToSharedRef(), 20);
	GEngine->GameViewport->AddViewportWidgetContent(Panel.ToSharedRef(), 30);
	Dialogue->SetVisibility(EVisibility::Collapsed);
	Panel->SetVisibility(EVisibility::Collapsed);

	if (URPGStory* S = URPGStory::Get(this)) S->OnDialogueChanged.AddUObject(this, &ARPGPlayerController::OnDialogueChanged);
	ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn());
	if (P) P->BindUIHooks([this](FName K) { HandleKey(K); });

	// Automated runs: ignore the real keyboard/mouse so someone typing elsewhere can't steer the test.
	bNoInput = FParse::Param(FCommandLine::Get(), TEXT("RPGNoInput"));
	if (P) P->bInputLocked = bNoInput;

	FString Class;
	if (FParse::Value(FCommandLine::Get(), TEXT("RPGClass="), Class)) EnterGameplay();
	else ShowCharSelect();
}

void ARPGPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GEngine && GEngine->GameViewport)
	{
		for (TSharedPtr<SWidget> Wd : TArray<TSharedPtr<SWidget>>{ Hud, Dialogue, CharSelect, Panel })
			if (Wd) GEngine->GameViewport->RemoveViewportWidgetContent(Wd.ToSharedRef());
	}
	Super::EndPlay(Reason);
}

void ARPGPlayerController::EnterGameplay()
{
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
	// The game never sees button releases while UI is up, so drop anything held (attack, block, bow, movement).
	if (ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn())) P->ClearHeldInput();
}

// ---------------------------------------------------------------------------------------------
// Character select
// ---------------------------------------------------------------------------------------------

void ARPGPlayerController::ShowCharSelect()
{
	ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn());
	if (!P) return;
	bCharSelect = true;

	// Preview camera: in front of the hero, slightly to the side so the panel on the right doesn't cover them.
	const FVector Fwd = P->GetActorForwardVector(), Right = P->GetActorRightVector();
	const FVector CamAt = P->GetActorLocation() + Fwd * 330.f + Right * 160.f + FVector(0, 0, 35.f);
	PreviewCam = GetWorld()->SpawnActor<ACameraActor>(CamAt, (P->Chest() + Right * 120.f - CamAt).Rotation());
	PreviewCam->GetCameraComponent()->SetFieldOfView(55.f);
	PreviewCam->GetCameraComponent()->bConstrainAspectRatio = false;
	bAutoManageActiveCameraTarget = false;
	SetViewTarget(PreviewCam);

	SAssignNew(CharSelect, SRPGCharSelect).World(GetWorld())
		.OnBegin(SRPGCharSelect::FOnBegin::CreateLambda([this](const FString& C, const FString& S) { if (!bNoInput) BeginGame(C, S); }))
		.OnPreview(SRPGCharSelect::FOnPreview::CreateLambda([this](const FString& C, const FString& S)
		{
			if (ARPGPlayerCharacter* PP = Cast<ARPGPlayerCharacter>(GetPawn())) PP->ApplyClass(C, S);
		}));
	GEngine->GameViewport->AddViewportWidgetContent(CharSelect.ToSharedRef(), 40);
	Hud->SetVisibility(EVisibility::Collapsed);

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(CharSelect);
	SetInputMode(Mode);
	bShowMouseCursor = true;
}

void ARPGPlayerController::BeginGame(const FString& ClassId, const FString& Sex)
{
	if (ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(GetPawn()))
	{
		P->ApplyClass(ClassId, Sex);
		SetControlRotation(P->GetActorRotation());
	}
	bCharSelect = false;
	GEngine->GameViewport->RemoveViewportWidgetContent(CharSelect.ToSharedRef());
	CharSelect.Reset();
	if (PreviewCam) { PreviewCam->Destroy(); PreviewCam = nullptr; }
	bAutoManageActiveCameraTarget = true;
	SetViewTarget(GetPawn());
	Hud->SetVisibility(EVisibility::SelfHitTestInvisible);
	EnterGameplay();

	URPGStory* S = URPGStory::Get(this);
	S->Toast(FString::Printf(TEXT("%s %s — talk to Elder Maren to begin."), Sex == TEXT("female") ? TEXT("Female") : TEXT("Male"),
		*Cast<ARPGPlayerCharacter>(GetPawn())->DisplayName), Cast<ARPGPlayerCharacter>(GetPawn())->NameColor);
	S->Toast(TEXT("Press H for controls"));
}

// ---------------------------------------------------------------------------------------------
// Dialogue & panels
// ---------------------------------------------------------------------------------------------

void ARPGPlayerController::OnDialogueChanged()
{
	URPGStory* S = URPGStory::Get(this);
	if (!S || !Dialogue) return;
	if (S->IsDialogueOpen())
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
	if (bCharSelect) return;
	if (Key == TEXT("Escape")) { if (bPanelOpen) ClosePanel(); return; }
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

void ARPGPlayerController::ClosePanel()
{
	bPanelOpen = false;
	Panel->SetVisibility(EVisibility::Collapsed);
	UGameplayStatics::SetGamePaused(this, false);
	EnterGameplay();
}
