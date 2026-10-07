#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RPGHUD.generated.h"

/**
 * World-anchored HUD drawn on the canvas each frame: floating combat text, enemy health bars,
 * names over talkable characters, quest markers (! / ?), the talk prompt, crosshair, and threat
 * arrows at the screen edge for unseen enemies that are hunting you.
 * Screen-space UI (bars, ability bar, panels, dialogue) is Slate — see SRPGWidgets.
 */
UCLASS()
class OATHBOUND_API ARPGHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void Text(const FString& S, float X, float Y, const FLinearColor& C, float Scale, bool bCenter = true, bool bShadow = true);
	void Bar(float X, float Y, float W, float H, float Frac, const FLinearColor& C);
};
