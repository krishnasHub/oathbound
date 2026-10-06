#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RPGPlayerController.generated.h"

class SRPGHud;
class SRPGDialogue;
class SRPGCharSelect;
class SRPGPanel;
class SRPGPauseMenu;
class SRPGTitle;
class SRPGCursor;
class ACameraActor;
class SWidget;

/**
 * Owns the Slate UI (HUD, dialogue, character select, panels) and switches input between gameplay and UI.
 * Character select shows a preview camera on the hero; picking a class/sex updates the model live.
 */
UCLASS()
class ACTIONRPG_API ARPGPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARPGPlayerController();

	void EnterGameplay();
	void EnterUI(TSharedPtr<SWidget> FocusWidget = nullptr);
	/** Title screen (Start New Game / Quit) over a slow drifting view of the world. */
	void ShowTitle();
	void ShowCharSelect();
	bool IsInTitle() const { return bTitle; }
	/** Playing (no menu, dialogue, panel, title or character select up): the game cursor shows. */
	bool IsInGameplay() const { return !bTitle && !bCharSelect && !bPanelOpen && !bPauseMenu && bShowMouseCursor && CurrentMouseCursor == EMouseCursor::None; }
	void TogglePanel(FName Mode);
	void ClosePanel();
	bool IsInCharSelect() const { return bCharSelect; }

	/** Pause menu (Esc): the world pauses behind Resume / New Game / Quit Game. */
	void OpenPauseMenu();
	void ResumeGame();
	/** Back to character select with a fresh world (no saves yet, so the current run is discarded). */
	void NewGame();
	void QuitGame();
	bool IsPauseMenuOpen() const { return bPauseMenu; }
	TSharedPtr<SRPGDialogue> GetDialogue() const { return Dialogue; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void OnDialogueChanged();
	void BeginGame(const FString& ClassId, const FString& Sex);
	void HandleKey(FName Key);

	TSharedPtr<SRPGHud> Hud;
	TSharedPtr<SRPGDialogue> Dialogue;
	TSharedPtr<SRPGCharSelect> CharSelect;
	TSharedPtr<SRPGPanel> Panel;
	TSharedPtr<SRPGPauseMenu> PauseMenu;
	TSharedPtr<SRPGTitle> Title;
	TSharedPtr<SRPGCursor> Cursor;
	bool bTitle = false;
	float TitleT = 0.f;
	FVector TitleFrom = FVector::ZeroVector;
	void HideTitle();
	/** The title and character select stand in front of the world: hide the hero and park the camera. */
	void SetHeroHidden(bool bHide);
	UPROPERTY() TObjectPtr<ACameraActor> PreviewCam;
	bool bCharSelect = false;
	bool bPanelOpen = false;
	bool bPauseMenu = false;
	bool bNoInput = false;
};
