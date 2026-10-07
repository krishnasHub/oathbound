#include "RPGHUD.h"
#include "TSFeedback.h"
#include "TSSky.h"
#include "RPGWorldBuilder.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "TSData.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "RPGPlayerCharacter.h"
#include "RPGPlayerController.h"
#include "TSHUDDraw.h"
#include "TSHeroControl.h"
#include "TSCameraRig.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "CanvasItem.h"

void ARPGHUD::Text(const FString& S, float X, float Y, const FLinearColor& C, float Scale, bool bCenter, bool bShadow)
{
	TSHUDDraw::Text(Canvas, S, X, Y, C, Scale, bCenter, bShadow);
}

void ARPGHUD::Bar(float X, float Y, float W, float H, float Frac, const FLinearColor& C)
{
	TSHUDDraw::Bar(Canvas, X, Y, W, H, Frac, C);
}

void ARPGHUD::DrawHUD()
{
	if (const ARPGPlayerController* PCtl = Cast<ARPGPlayerController>(PlayerOwner); PCtl && (PCtl->IsInTitle() || PCtl->IsInCharSelect())) { Super::DrawHUD(); return; }
	Super::DrawHUD();
	URPGSession* Session = URPGSession::Get(this);
	ARPGPlayerCharacter* P = Session ? Session->Player() : nullptr;
	if (!Session || !P) return;
	const float UI = Canvas->ClipY / 1080.f;   // scale with resolution

	// Characters: health bars, names, quest markers.
	for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It)
	{
		ARPGCharacterBase* C = *It;
		if (C == P || C->IsDead() || C->IsHidden()) continue;
		const float Dist = FVector::Dist(C->GetActorLocation(), P->GetActorLocation());
		if (Dist > 3500.f) continue;
		if (!ATSSky::IsLit(C->GetActorLocation(), P->GetActorLocation())) continue;   // lost in the dark
		const FVector S = Project(C->Head() + FVector(0, 0, 45.f), false);
		if (S.Z <= 0.f) continue;   // behind the camera

		ARPGEnemy* E = Cast<ARPGEnemy>(C);
		const bool bTalkable = !C->DialogueRoot.IsEmpty() && (C->Team == ETSTeam::Neutral || C->IsPassive());
		if (bTalkable && Dist < 2200.f)
		{
			Text(C->DisplayName, S.X, S.Y + 10 * UI, C->NameColor, 1.1f * UI);
			const FString Marker = Session->MarkerFor(C);
			if (!Marker.IsEmpty())
			{
				const float Bob = FMath::Sin(GetWorld()->GetRealTimeSeconds() * 4.f) * 5.f * UI - 14.f * UI;
				Text(Marker, S.X, S.Y - 22 * UI + Bob, Marker == TEXT("?") ? FLinearColor(0.44f, 0.88f, 0.54f) : FLinearColor(1.f, 0.83f, 0.3f), 3.f * UI);
			}
		}
		if (E && !E->IsBoss() && C->Stats->Health() < C->Stats->MaxHealth() && Dist < 2500.f)
		{
			const float W = 70.f * UI;
			Bar(S.X - W * 0.5f, S.Y, W, 6.f * UI, C->Stats->Health() / C->Stats->MaxHealth(), FLinearColor(0.84f, 0.27f, 0.27f));
		}
		if (E && E->Tags.Has(TEXT("Marked")))
		{
			const TSJson::FObj Next = E->CurrentAttack() ? E->CurrentAttack() : nullptr;
			Text(FString::Printf(TEXT("%.0f / %.0f HP%s"), E->Stats->Health(), E->Stats->MaxHealth(), Next ? *(TEXT("  next: ") + TSJson::Str(Next, TEXT("type"))) : TEXT("")),
				S.X, S.Y - 16 * UI, FLinearColor(1.f, 0.83f, 0.3f), 0.6f * UI);
		}
		if (Session->IsDebug() && E)
		{
			static const TCHAR* Names[] = { TEXT("idle"), TEXT("chase"), TEXT("windup"), TEXT("recover"), TEXT("return"), TEXT("leaving") };
			Text(FString::Printf(TEXT("%s  %s%s  poise %.0f"), Names[uint8(E->State)], *E->Region, E->bProvoked ? TEXT(" PROVOKED") : TEXT(""), E->Poise),
				S.X, S.Y + 24 * UI, FLinearColor(0.3f, 1.f, 0.3f), 0.55f * UI, true);
		}
	}

	// Floating combat text, and threat sense: arrows at the screen edge pointing at unseen enemies hunting you.
	TSHUDDraw::Floaters(Canvas, this, UI);
	TSHUDDraw::ThreatArrows(Canvas, PlayerOwner, P, UI);
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);

	if (Session->Story()->IsDialogueOpen() || P->IsDead()) return;

	// Crosshair for ranged styles / aiming (third person; top-down aims with the mouse cursor).
	const TSJson::FObj Style = P->Style();
	const bool bTopDown = UTSCameraRig::IsTopDown(this);
	const bool bRanged = !bTopDown
		&& (TSJson::Str(TSJson::Obj(Style, TEXT("primary")), TEXT("type")) == TEXT("bolt") || P->IsDrawing());
	if (bRanged)
	{
		const float R = 4.f * UI;
		FCanvasTileItem Dot(Center - FVector2D(R * 0.5f, R * 0.5f), FVector2D(R, R), FLinearColor(1, 1, 1, 0.85f));
		Dot.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Dot);
	}

	// Top-down: a ring where a click-to-move is heading, and the name of what the cursor is on.
	if (bTopDown)
	{
		FVector Dest;
		if (P->Control->ClickDestination(Dest))
			TSHUDDraw::GroundRing(Canvas, Dest, 38.f + 6.f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 8.f), FLinearColor(1.f, 0.9f, 0.55f, 0.85f), 2.f * UI);
		// Hover: red = a click fights them; white = a click talks; grey = can't talk (talk mode).
		bool bHostile = false;
		const ATSCharacter* H = P->Control->UnderCursor(bHostile);
		if (H)
		{
			const FVector S = Project(H->Head() + FVector(0, 0, 40), false);
			const bool bTalk = P->Control->IsTalkMode() || !bHostile;
			const FString Why = bTalk ? P->TalkBlocker(H) : FString();
			const FLinearColor C = !bTalk ? FLinearColor(1.f, 0.35f, 0.3f) : Why.IsEmpty() ? FLinearColor(0.95f, 0.95f, 0.85f) : FLinearColor(0.6f, 0.6f, 0.6f);
			if (S.Z > 0) Text(bTalk ? TEXT("Talk: ") + H->DisplayName : H->DisplayName, S.X, S.Y, C, 0.8f * UI);
		}
		// Talk mode: a label next to the cursor.
		float MX = 0.f, MY = 0.f;
		if (P->Control->IsTalkMode() && PlayerOwner->GetMousePosition(MX, MY))
			Text(TEXT("TALK  (click someone · E / RMB to cancel)"), MX + 18.f * UI, MY + 26.f * UI, FLinearColor(0.95f, 0.85f, 0.5f), 0.65f * UI, false);
	}

	// Talk prompt (third person; top-down talks with the cursor).
	if (ARPGCharacterBase* T = bTopDown ? nullptr : P->TalkTarget())
	{
		Text(FString::Printf(TEXT("[E] Talk to %s"), *T->DisplayName), Center.X, Canvas->ClipY * 0.72f, FLinearColor::White, 1.f * UI);
	}
}
