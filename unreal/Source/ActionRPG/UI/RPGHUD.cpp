#include "RPGHUD.h"
#include "RPGStory.h"
#include "RPGData.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "RPGPlayerCharacter.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "CanvasItem.h"

void ARPGHUD::Text(const FString& S, float X, float Y, const FLinearColor& C, float Scale, bool bCenter, bool bShadow)
{
	UFont* Font = GEngine->GetLargeFont();
	float W = 0, H = 0;
	Canvas->TextSize(Font, S, W, H, Scale, Scale);
	if (bCenter) X -= W * 0.5f;
	FCanvasTextItem Item(FVector2D(X, Y - H * 0.5f), FText::FromString(S), Font, C);
	Item.Scale = FVector2D(Scale, Scale);
	if (bShadow) { Item.EnableShadow(FLinearColor(0, 0, 0, 0.85f), FVector2D(1.5f, 1.5f)); }
	Canvas->DrawItem(Item);
}

void ARPGHUD::Bar(float X, float Y, float W, float H, float Frac, const FLinearColor& C)
{
	FCanvasTileItem Back(FVector2D(X - 1, Y - 1), FVector2D(W + 2, H + 2), FLinearColor(0, 0, 0, 0.7f));
	Back.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Back);
	FCanvasTileItem Fill(FVector2D(X, Y), FVector2D(W * FMath::Clamp(Frac, 0.f, 1.f), H), C);
	Fill.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Fill);
}

void ARPGHUD::DrawHUD()
{
	Super::DrawHUD();
	URPGStory* Story = URPGStory::Get(this);
	ARPGPlayerCharacter* P = Story ? Story->Player() : nullptr;
	if (!Story || !P) return;
	const float UI = Canvas->ClipY / 1080.f;   // scale with resolution

	// Characters: health bars, names, quest markers.
	for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It)
	{
		ARPGCharacterBase* C = *It;
		if (C == P || C->IsDead() || C->IsHidden()) continue;
		const float Dist = FVector::Dist(C->GetActorLocation(), P->GetActorLocation());
		if (Dist > 3500.f) continue;
		const FVector S = Project(C->Head() + FVector(0, 0, 45.f), false);
		if (S.Z <= 0.f) continue;   // behind the camera

		ARPGEnemy* E = Cast<ARPGEnemy>(C);
		const bool bTalkable = !C->DialogueRoot.IsEmpty() && (C->Team == ERPGTeam::Villager || C->IsPassive());
		if (bTalkable && Dist < 2200.f)
		{
			Text(C->DisplayName, S.X, S.Y + 10 * UI, C->NameColor, 1.1f * UI);
			const FString Marker = Story->MarkerFor(C);
			if (!Marker.IsEmpty())
			{
				const float Bob = FMath::Sin(GetWorld()->GetRealTimeSeconds() * 4.f) * 5.f * UI - 14.f * UI;
				Text(Marker, S.X, S.Y - 22 * UI + Bob, Marker == TEXT("?") ? FLinearColor(0.44f, 0.88f, 0.54f) : FLinearColor(1.f, 0.83f, 0.3f), 3.f * UI);
			}
		}
		if (E && !E->IsBoss() && C->Stats->HP < C->Stats->MaxHP() && Dist < 2500.f)
		{
			const float W = 70.f * UI;
			Bar(S.X - W * 0.5f, S.Y, W, 6.f * UI, C->Stats->HP / C->Stats->MaxHP(), FLinearColor(0.84f, 0.27f, 0.27f));
		}
		if (E && E->Tags.Has(TEXT("Marked")))
		{
			const RPGJson::FObj Next = E->CurrentAttack() ? E->CurrentAttack() : nullptr;
			Text(FString::Printf(TEXT("%.0f / %.0f HP%s"), E->Stats->HP, E->Stats->MaxHP(), Next ? *(TEXT("  next: ") + RPGJson::Str(Next, TEXT("type"))) : TEXT("")),
				S.X, S.Y - 16 * UI, FLinearColor(1.f, 0.83f, 0.3f), 0.6f * UI);
		}
		if (Story->bDebug && E)
		{
			static const TCHAR* Names[] = { TEXT("idle"), TEXT("chase"), TEXT("windup"), TEXT("recover"), TEXT("return"), TEXT("leaving") };
			Text(FString::Printf(TEXT("%s  %s%s  poise %.0f"), Names[uint8(E->State)], *E->Region, E->bProvoked ? TEXT(" PROVOKED") : TEXT(""), E->Poise),
				S.X, S.Y + 24 * UI, FLinearColor(0.3f, 1.f, 0.3f), 0.55f * UI, true);
		}
	}

	// Floating combat text.
	for (const FRPGFloater& F : Story->Floaters)
	{
		const FVector S = Project(F.World, false);
		if (S.Z <= 0.f) continue;
		FLinearColor C = F.Color;
		C.A = FMath::Clamp(2.f * (1.f - F.Age / F.Life), 0.f, 1.f);
		Text(F.Text, S.X, S.Y, C, 1.1f * F.Size * UI);
	}

	// Threat sense: arrows at the screen edge pointing at unseen enemies hunting you; red while they wind up.
	const URPGData& D = URPGData::Get(this);
	const float ThreatRange = D.Px(RPGJson::Num(RPGJson::Obj(D.Section(TEXT("tuning")), TEXT("threatSense")), TEXT("range"), 450));
	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It)
	{
		ARPGEnemy* E = *It;
		if (E->IsDead() || E->IsPassive() || (E->State != ERPGEnemyState::Chase && E->State != ERPGEnemyState::Windup && E->State != ERPGEnemyState::Recover)) continue;
		if (FVector::Dist(E->GetActorLocation(), P->GetActorLocation()) > ThreatRange) continue;
		const FVector S = Project(E->Chest(), false);
		const bool bOnScreen = S.Z > 0 && S.X > 0 && S.Y > 0 && S.X < Canvas->ClipX && S.Y < Canvas->ClipY;
		if (bOnScreen) continue;
		// Direction relative to the camera, mapped onto the screen.
		const FRotator CamRot = PlayerOwner->PlayerCameraManager->GetCameraRotation();
		const FVector Local = FRotator(0, CamRot.Yaw, 0).UnrotateVector(E->GetActorLocation() - P->GetActorLocation());
		const FVector2D Dir = FVector2D(Local.Y, -Local.X).GetSafeNormal();
		const FVector2D At = Center + Dir * FMath::Min(Canvas->ClipX, Canvas->ClipY) * 0.42f;
		const bool bWinding = E->State == ERPGEnemyState::Windup;
		FLinearColor C = bWinding ? FLinearColor(1.f, 0.15f, 0.15f) : FLinearColor(1.f, 0.55f, 0.4f);
		C.A = bWinding ? 0.7f + 0.3f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 20.f) : 0.6f;
		const FVector2D Perp(-Dir.Y, Dir.X);
		const float L = 26.f * UI, Wd = 14.f * UI;
		FCanvasTriangleItem Tri(At + Dir * L, At + Perp * Wd, At - Perp * Wd, GWhiteTexture);
		Tri.SetColor(C);
		Tri.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tri);
	}

	if (Story->IsDialogueOpen() || P->IsDead()) return;

	// Crosshair for ranged styles / aiming.
	const RPGJson::FObj Style = P->Style();
	const bool bRanged = RPGJson::Str(RPGJson::Obj(Style, TEXT("primary")), TEXT("type")) == TEXT("bolt") || P->IsDrawing();
	if (bRanged)
	{
		const float R = 4.f * UI;
		FCanvasTileItem Dot(Center - FVector2D(R * 0.5f, R * 0.5f), FVector2D(R, R), FLinearColor(1, 1, 1, 0.85f));
		Dot.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Dot);
	}

	// Talk prompt.
	if (ARPGCharacterBase* T = P->TalkTarget())
	{
		Text(FString::Printf(TEXT("[E] Talk to %s"), *T->DisplayName), Center.X, Canvas->ClipY * 0.72f, FLinearColor::White, 1.f * UI);
	}
}
