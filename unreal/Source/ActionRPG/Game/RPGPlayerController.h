#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RPGPlayerController.generated.h"

class SRPGHud;
class SRPGDialogue;
class SRPGCharSelect;
class SRPGPanel;
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
	void ShowCharSelect();
	void TogglePanel(FName Mode);
	void ClosePanel();
	bool IsInCharSelect() const { return bCharSelect; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void OnDialogueChanged();
	void BeginGame(const FString& ClassId, const FString& Sex);
	void HandleKey(FName Key);

	TSharedPtr<SRPGHud> Hud;
	TSharedPtr<SRPGDialogue> Dialogue;
	TSharedPtr<SRPGCharSelect> CharSelect;
	TSharedPtr<SRPGPanel> Panel;
	UPROPERTY() TObjectPtr<ACameraActor> PreviewCam;
	bool bCharSelect = false;
	bool bPanelOpen = false;
	bool bNoInput = false;
};
