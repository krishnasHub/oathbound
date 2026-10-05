#include "SRPGWidgets.h"
#include "RPGStory.h"
#include "RPGData.h"
#include "RPGPlayerCharacter.h"
#include "RPGEnemy.h"
#include "RPGInventoryComponent.h"
#include "RPGAbilityComponent.h"
#include "RPGCombat.h"

#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "EngineUtils.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "RPGUI"

namespace
{
	FSlateFontInfo Font(int32 Size, const TCHAR* Style = TEXT("Regular")) { return FCoreStyle::GetDefaultFontStyle(Style, Size); }
	const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	const FLinearColor PanelColor(0.055f, 0.06f, 0.075f, 0.93f);
	const FLinearColor Gold(1.f, 0.83f, 0.3f);
	const FLinearColor Muted(0.62f, 0.64f, 0.68f);

	URPGStory* StoryOf(const TWeakObjectPtr<UWorld>& W) { return W.IsValid() ? W->GetSubsystem<URPGStory>() : nullptr; }
	ARPGPlayerCharacter* PlayerOf(const TWeakObjectPtr<UWorld>& W) { URPGStory* S = StoryOf(W); return S ? S->Player() : nullptr; }
	FText T(const FString& S) { return FText::FromString(S); }

	/** A horizontal bar: dark track, coloured fill (fraction from a lambda), optional label. */
	TSharedRef<SWidget> Bar(float Width, float Height, TAttribute<FSlateColor> Color, TFunction<float()> Frac, TFunction<FString()> Label = nullptr)
	{
		return SNew(SBox).WidthOverride(Width).HeightOverride(Height)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0, 0, 0, 0.62f)) ]
			+ SOverlay::Slot().HAlign(HAlign_Left)
			[
				SNew(SBox).WidthOverride_Lambda([Width, Frac]() { return FOptionalSize(Width * FMath::Clamp(Frac(), 0.f, 1.f)); })
				[ SNew(SImage).Image(White()).ColorAndOpacity(Color) ]
			]
			+ SOverlay::Slot().VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ShadowOffset(FVector2D(1, 1))
				.Text_Lambda([Label]() { return Label ? T(Label()) : FText::GetEmpty(); })
			]
		];
	}

	/** Same numbers as the prototype's character-select preview, straight from the data. */
	struct FClassPreview { float HP, Stamina, Mana, Speed, Armor, Attack, Magic, Crit; };
	FClassPreview Preview(const URPGData& D, const FString& ClassId)
	{
		const RPGJson::FObj C = D.Entry(TEXT("classes"), ClassId);
		const RPGJson::FObj A = RPGJson::Obj(C, TEXT("attributes"));
		TMap<FString, double> Gear;
		for (const auto& V : RPGJson::Arr(C, TEXT("startItems")))
		{
			if (!RPGJson::Bool(V->AsObject(), TEXT("equip"))) continue;
			const RPGJson::FObj Mods = RPGJson::Obj(D.Entry(TEXT("items"), RPGJson::Str(V->AsObject(), TEXT("item"))), TEXT("mods"));
			if (Mods) for (const auto& KV : Mods->Values) Gear.FindOrAdd(FString(*KV.Key)) += KV.Value->AsNumber();
		}
		auto Attr = [&](const TCHAR* K) { return RPGJson::Num(A, K) + Gear.FindRef(K); };
		FClassPreview P;
		P.HP = float(RPGJson::Num(C, TEXT("hpBase")) + Attr(TEXT("vitality")) * D.Tuning(TEXT("hpPerVitality"), 10) + D.Tuning(TEXT("hpPerLevel"), 5));
		P.Stamina = float(D.Tuning(TEXT("staminaBase"), 80) + Attr(TEXT("agility")) * D.Tuning(TEXT("staminaPerAgility"), 4));
		P.Mana = float(RPGJson::Num(C, TEXT("manaBase")) + Attr(TEXT("focus")) * D.Tuning(TEXT("manaPerFocus"), 5));
		P.Speed = float(RPGJson::Num(C, TEXT("moveSpeed")));
		P.Armor = float(Gear.FindRef(TEXT("armor")));
		P.Crit = float(RPGJson::Num(C, TEXT("critPct")) + Gear.FindRef(TEXT("critPct")) + Attr(TEXT("agility")) * D.Tuning(TEXT("critPctPerAgility"), 1));
		P.Magic = float(1.0 + Attr(TEXT("focus")) * D.Tuning(TEXT("focusScaling"), 0.07));
		const RPGJson::FObj Style = D.Entry(TEXT("weaponStyles"), RPGJson::Arr(C, TEXT("styles"))[0]->AsString());
		const RPGJson::FObj Pr = RPGJson::Obj(Style, TEXT("primary"));
		const FString Scale = RPGJson::Str(Pr, TEXT("scaling"), TEXT("might"));
		const double ScaleMul = 1.0 + Attr(*Scale) * D.Tuning(*(Scale + TEXT("Scaling")), 0.06);
		const double Weapon = RPGJson::Num(D.Section(TEXT("player")), TEXT("unarmedDamage"), 4) + Gear.FindRef(TEXT("weaponDamage"));
		P.Attack = RPGJson::Str(Pr, TEXT("type")) == TEXT("bolt")
			? float((RPGJson::Num(Pr, TEXT("damage")) + Weapon * RPGJson::Num(Pr, TEXT("weaponRatio"), 0.5)) * ScaleMul)
			: float(Weapon * RPGJson::Num(RPGJson::Arr(Pr, TEXT("combo"))[0]->AsObject(), TEXT("mult"), 1) * RPGJson::Num(Style, TEXT("damageMul"), 1) * ScaleMul);
		return P;
	}
}

// =============================================================================================
// HUD
// =============================================================================================

void SRPGHud::Construct(const FArguments& Args)
{
	World = Args._World;
	TWeakObjectPtr<UWorld> W = World;

	auto Vitals = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1, 1))
			.Text_Lambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); if (!P) return FText::GetEmpty();
				return T(FString::Printf(TEXT("Lv %d  %s %s%s"), P->Level(), *FString(P->Sex == TEXT("female") ? TEXT("Female") : TEXT("Male")), *P->DisplayName,
					P->AttrPoints ? *FString::Printf(TEXT("   (+%d points — C)"), P->AttrPoints) : TEXT(""))); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[ Bar(320, 22, FLinearColor(0.78f, 0.2f, 0.2f), [W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return P ? P->Stats->HP / FMath::Max(1.f, P->Stats->MaxHP()) : 0.f; },
			[W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return P ? FString::Printf(TEXT("HP %.0f / %.0f"), FMath::CeilToFloat(P->Stats->HP), P->Stats->MaxHP()) : FString(); }) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[ Bar(320, 10, TAttribute<FSlateColor>::CreateLambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return FSlateColor(P && P->Stats->StaminaDelay > 0 ? FLinearColor(0.33f, 0.6f, 0.3f) : FLinearColor(0.5f, 0.82f, 0.45f)); }),
			[W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return P ? P->Stats->Stamina / FMath::Max(1.f, P->Stats->MaxStamina()) : 0.f; }) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[ Bar(320, 10, FLinearColor(0.3f, 0.48f, 0.9f), [W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return P ? P->Stats->Mana / FMath::Max(1.f, P->Stats->MaxMana()) : 0.f; }) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
		[
			SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor(0.37f, 0.88f, 0.54f)).ShadowOffset(FVector2D(1, 1))
			.Text_Lambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); if (!P) return FText::GetEmpty();
				FString S;
				for (const FRPGEffect& E : P->Stats->Effects) S += FString::Printf(TEXT("%s%s  %.1fs\n"), *E.Name, E.Absorb > 0 ? *FString::Printf(TEXT(" (%.0f)"), E.Absorb) : TEXT(""), E.Remaining);
				if (P->Tags.Has(TEXT("Hidden"))) S += TEXT("Hidden\n");
				if (P->Tags.Has(TEXT("Staggered"))) S += TEXT("Staggered\n");
				return T(S); })
		];

	auto Tracker = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(STextBlock).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(Gold).ShadowOffset(FVector2D(1, 1))
			.Text_Lambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return P ? T(FString::Printf(TEXT("%d gold"), P->Inventory->Gold)) : FText::GetEmpty(); })
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 10)
		[
			SNew(STextBlock).Font(Font(12)).Justification(ETextJustify::Right).ShadowOffset(FVector2D(1, 1))
			.Text_Lambda([W]() { const URPGStory* S = StoryOf(W); if (!S) return FText::GetEmpty();
				FString Out;
				for (const auto& KV : S->Quests)
				{
					if (KV.Value.Status == TEXT("turnedIn")) continue;
					const RPGJson::FObj Q = URPGData::Get(W.Get()).Entry(TEXT("quests"), KV.Key);
					Out += RPGJson::Str(Q, TEXT("name")) + TEXT("\n   ") + (KV.Value.Status == TEXT("complete") ? TEXT("Return to ") + RPGJson::Str(Q, TEXT("giver")) : S->ProgressText(KV.Key)) + TEXT("\n\n");
				}
				return T(Out); })
		];

	auto Toasts = SNew(SVerticalBox);
	for (int32 I = 0; I < 5; ++I)
	{
		Toasts->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.6f)).Padding(FMargin(14, 5))
			.Visibility_Lambda([W, I]() { const URPGStory* S = StoryOf(W); return S && S->Toasts.IsValidIndex(I) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(STextBlock).Font(Font(13))
				.Text_Lambda([W, I]() { const URPGStory* S = StoryOf(W); return S && S->Toasts.IsValidIndex(I) ? T(S->Toasts[I].Text) : FText::GetEmpty(); })
				.ColorAndOpacity_Lambda([W, I]() { const URPGStory* S = StoryOf(W); if (!S || !S->Toasts.IsValidIndex(I)) return FSlateColor(FLinearColor::White);
					FLinearColor C = S->Toasts[I].Color; C.A = FMath::Clamp((3.4f - S->Toasts[I].Age) * 2.f, 0.f, 1.f); return FSlateColor(C); })
			]
		];
	}

	auto BossBar = SNew(SVerticalBox)
		.Visibility_Lambda([W]() {
			if (!W.IsValid()) return EVisibility::Collapsed;
			for (TActorIterator<ARPGEnemy> It(W.Get()); It; ++It)
				if (It->IsBoss() && !It->IsDead() && !It->IsPassive() && (It->State == ERPGEnemyState::Chase || It->State == ERPGEnemyState::Windup || It->State == ERPGEnemyState::Recover)) return EVisibility::HitTestInvisible;
			return EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Font(Font(14, TEXT("Bold"))).ShadowOffset(FVector2D(1, 1))
			.Text_Lambda([W]() { for (TActorIterator<ARPGEnemy> It(W.Get()); It; ++It) if (It->IsBoss() && !It->IsDead() && !It->IsPassive() && It->State != ERPGEnemyState::Idle) return T(It->DisplayName); return FText::GetEmpty(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			Bar(560, 14, FLinearColor(0.7f, 0.17f, 0.17f), [W]() {
				for (TActorIterator<ARPGEnemy> It(W.Get()); It; ++It) if (It->IsBoss() && !It->IsDead() && !It->IsPassive() && It->State != ERPGEnemyState::Idle) return It->Stats->HP / It->Stats->MaxHP();
				return 0.f; })
		];

	auto Slots = SNew(SHorizontalBox);
	for (int32 I = 0; I < 5; ++I) Slots->AddSlot().AutoWidth().Padding(4, 0)[ AbilitySlot(I) ];

	ChildSlot
	[
		SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24)[ Vitals ]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)[ Tracker ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 28)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ BossBar ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14)[ Toasts ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 22)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8)
			[
				SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor(0.85f, 0.87f, 0.9f)).ShadowOffset(FVector2D(1, 1))
				.Text_Lambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); if (!P) return FText::GetEmpty();
					const RPGJson::FObj St = P->Style();
					const int32 N = RPGJson::Arr(P->ClassDef, TEXT("styles")).Num();
					FString Swap;
					if (N > 1) Swap = FString::Printf(TEXT("   ·   X: switch to %s"), *RPGJson::Str(URPGData::Get(W.Get()).Entry(TEXT("weaponStyles"), RPGJson::Arr(P->ClassDef, TEXT("styles"))[(P->StyleIndex + 1) % N]->AsString()), TEXT("name")));
					return T(RPGJson::Str(St, TEXT("name")) + TEXT(" — ") + RPGJson::Str(St, TEXT("hint")) + Swap); })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Slots ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
		[
			SNew(SBox).HeightOverride(5)
			[
				SNew(SProgressBar).FillColorAndOpacity(FLinearColor(0.7f, 0.55f, 1.f))
				.Percent_Lambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return TOptional<float>(P ? float(P->Xp) / ARPGPlayerCharacter::XpToNext(P, P->Level()) : 0.f); })
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Font(Font(60, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.84f, 0.27f, 0.27f)).ShadowOffset(FVector2D(2, 2))
			.Text(LOCTEXT("Died", "YOU DIED"))
			.Visibility_Lambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return P && P->IsDead() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		]
	];
}

TSharedRef<SWidget> SRPGHud::AbilitySlot(int32 Index)
{
	TWeakObjectPtr<UWorld> W = World;
	const bool bPotion = Index == 4;
	auto Def = [W, Index]() -> RPGJson::FObj { const ARPGPlayerCharacter* P = PlayerOf(W); return P && P->Abilities->Ids.IsValidIndex(Index) ? P->Abilities->Def(P->Abilities->Ids[Index]) : nullptr; };
	const float Size = 64.f;

	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
	[
		SNew(SBorder).BorderImage(White()).Padding(2)
		.BorderBackgroundColor_Lambda([W, Index, bPotion, Def]() {
			const ARPGPlayerCharacter* P = PlayerOf(W);
			if (!P) return FSlateColor(FLinearColor::Gray);
			if (bPotion) return FSlateColor(P->Inventory->Count(TEXT("potion")) ? FLinearColor(0.84f, 0.27f, 0.27f) : FLinearColor(0.25f, 0.25f, 0.25f));
			const RPGJson::FObj D = Def();
			const bool bReady = D && P->Abilities->Unlocked(P->Abilities->Ids[Index]) && P->Abilities->Cooldowns.FindRef(P->Abilities->Ids[Index]) <= 0.f && P->Abilities->CanAfford(D);
			return FSlateColor(bReady ? RPGJson::Color(RPGJson::Str(D, TEXT("color"))) : FLinearColor(0.25f, 0.25f, 0.27f)); })
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.07f, 0.075f, 0.09f, 0.92f)) ]
			+ SOverlay::Slot().VAlign(VAlign_Bottom)
			[
				// Cooldown sweep (or a full dark cover while locked).
				SNew(SBox).HeightOverride_Lambda([W, Index, bPotion, Def, Size]() {
					const ARPGPlayerCharacter* P = PlayerOf(W);
					if (bPotion || !P || !P->Abilities->Ids.IsValidIndex(Index)) return FOptionalSize(0.f);
					const FString Id = P->Abilities->Ids[Index];
					if (!P->Abilities->Unlocked(Id)) return FOptionalSize(Size);
					const float Cd = P->Abilities->Cooldowns.FindRef(Id), Max = float(RPGJson::Num(Def(), TEXT("cooldown"), 1));
					return FOptionalSize(Size * FMath::Clamp(Cd / Max, 0.f, 1.f)); })
				[ SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0, 0, 0, 0.7f)) ]
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(4, 1)
			[ SNew(STextBlock).Font(Font(9, TEXT("Bold"))).ColorAndOpacity(Muted).Text(T(bPotion ? TEXT("Q") : FString::FromInt(Index + 1))) ]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(3)
			[
				SNew(STextBlock).Font(Font(9)).Justification(ETextJustify::Center).AutoWrapText(true)
				.Text_Lambda([W, Index, bPotion, Def]() {
					const ARPGPlayerCharacter* P = PlayerOf(W);
					if (!P) return FText::GetEmpty();
					if (bPotion) return T(FString::Printf(TEXT("Potion\nx%d"), P->Inventory->Count(TEXT("potion"))));
					const RPGJson::FObj D = Def();
					if (!D) return FText::GetEmpty();
					const FString Id = P->Abilities->Ids[Index];
					if (!P->Abilities->Unlocked(Id)) return T(FString::Printf(TEXT("%s\nLv %d"), *RPGJson::Str(D, TEXT("name")), int32(RPGJson::Num(D, TEXT("unlockLevel"), 1))));
					const float Cd = P->Abilities->Cooldowns.FindRef(Id);
					return T(Cd > 0.f ? FString::Printf(TEXT("%.1f"), Cd) : RPGJson::Str(D, TEXT("name"))); })
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(4, 1)
			[
				SNew(STextBlock).Font(Font(9, TEXT("Bold")))
				.Text_Lambda([Def, bPotion]() { const RPGJson::FObj D = bPotion ? nullptr : Def(); if (!D) return FText::GetEmpty();
					return T(FString::FromInt(int32(RPGJson::Num(D, TEXT("mana"), RPGJson::Num(D, TEXT("stamina"), 0))))); })
				.ColorAndOpacity_Lambda([Def, bPotion]() { const RPGJson::FObj D = bPotion ? nullptr : Def();
					return FSlateColor(D && RPGJson::Has(D, TEXT("mana")) ? FLinearColor(0.56f, 0.69f, 1.f) : FLinearColor(0.5f, 0.82f, 0.5f)); })
			]
		]
	];
}

// =============================================================================================
// Dialogue
// =============================================================================================

void SRPGDialogue::Construct(const FArguments& Args)
{
	World = Args._World;
	TWeakObjectPtr<UWorld> W = World;
	ChildSlot
	.HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 140)
	[
		SNew(SBox).WidthOverride(820)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(PanelColor).Padding(FMargin(22, 16))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
				[
					SNew(STextBlock).Font(Font(15, TEXT("Bold")))
					.Text_Lambda([W]() { const URPGStory* S = StoryOf(W); return S ? T(S->DialogueSpeaker) : FText::GetEmpty(); })
					.ColorAndOpacity_Lambda([W]() { const URPGStory* S = StoryOf(W); return FSlateColor(S ? S->SpeakerColor : FLinearColor::White); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
				[
					SNew(STextBlock).Font(Font(13)).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.92f, 0.91f, 0.89f))
					.Text_Lambda([W]() { const URPGStory* S = StoryOf(W); return S ? T(S->DialogueText) : FText::GetEmpty(); })
				]
				+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(Choices, SVerticalBox) ]
			]
		]
	];
}

void SRPGDialogue::Refresh()
{
	Choices->ClearChildren();
	URPGStory* S = StoryOf(World);
	if (!S) return;
	for (int32 I = 0; I < S->ChoiceViews.Num(); ++I)
	{
		const FRPGChoiceView& V = S->ChoiceViews[I];
		TWeakObjectPtr<UWorld> W = World;
		Choices->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SButton).IsEnabled(V.bEnabled).ButtonColorAndOpacity(FLinearColor(0.16f, 0.18f, 0.22f))
			.OnClicked_Lambda([W, I]() { if (URPGStory* St = StoryOf(W)) St->Choose(I); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 2)[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Muted).Text(T(FString::Printf(TEXT("%d."), I + 1))) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2, 2)
				[ SNew(STextBlock).Font(Font(12, TEXT("Bold"))).ColorAndOpacity(V.VerbColor).Text(T(V.Verb.IsEmpty() ? FString() : TEXT("[") + V.Verb + TEXT("]"))) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(4, 2)[ SNew(STextBlock).Font(Font(12)).AutoWrapText(true).Text(T(V.Text)) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 2)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).Text(T(V.Odds)) ]
			]
		];
	}
}

void SRPGDialogue::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SCompoundWidget::Tick(G, Time, Dt);
	// Modal: if a click (or anything else) took keyboard focus away, take it back so 1-9 / Esc keep working.
	const URPGStory* S = StoryOf(World);
	if (S && S->IsDialogueOpen() && !HasKeyboardFocus()) FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
}

FReply SRPGDialogue::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	URPGStory* S = StoryOf(World);
	if (!S || !S->IsDialogueOpen()) return FReply::Unhandled();
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 I = 0; I < 9; ++I) if (E.GetKey() == Digits[I]) { S->Choose(I); return FReply::Handled(); }
	if (E.GetKey() == EKeys::Escape) { S->CloseDialogue(); return FReply::Handled(); }
	return FReply::Unhandled();
}

// =============================================================================================
// Character select
// =============================================================================================

void SRPGCharSelect::Construct(const FArguments& Args)
{
	World = Args._World;
	OnBegin = Args._OnBegin;
	OnPreview = Args._OnPreview;
	const RPGJson::FObj Classes = URPGData::Get(World.Get()).Section(TEXT("classes"));
	for (const auto& KV : Classes->Values) ClassIds.Add(FString(*KV.Key));

	ChildSlot
	.HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0, 0, 50, 0)
	[
		SNew(SBox).WidthOverride(860)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(PanelColor).Padding(FMargin(26, 20))
			[ SAssignNew(Body, SBox) ]
		]
	];
	Rebuild();
}

void SRPGCharSelect::Rebuild()
{
	const URPGData& D = URPGData::Get(World.Get());
	const RPGJson::FObj C = D.Entry(TEXT("classes"), ClassId);
	const FLinearColor Accent = RPGJson::Color(RPGJson::Str(C, TEXT("color")));

	// Class cards.
	auto Cards = SNew(SHorizontalBox);
	for (int32 I = 0; I < ClassIds.Num(); ++I)
	{
		const FString Id = ClassIds[I];
		const RPGJson::FObj K = D.Entry(TEXT("classes"), Id);
		const bool bSel = Id == ClassId;
		Cards->AddSlot().FillWidth(1).Padding(4)
		[
			SNew(SButton).ButtonColorAndOpacity(bSel ? FLinearColor(0.2f, 0.23f, 0.3f) : FLinearColor(0.11f, 0.12f, 0.15f))
			.OnClicked_Lambda([this, Id]() { Select(Id); return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[ SNew(STextBlock).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(RPGJson::Color(RPGJson::Str(K, TEXT("color")))).Text(T(FString::Printf(TEXT("%s   %d"), *RPGJson::Str(K, TEXT("name")), I + 1))) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
				[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted).AutoWrapText(true).Text(T(RPGJson::Str(K, TEXT("tagline")))) ]
			]
		];
	}

	// Attribute bars (out of 10) and derived stats scaled against the best class.
	auto Stats = SNew(SVerticalBox);
	auto Row = [&](const FString& Label, float Value, float Max, const FLinearColor& Col, const FString& Shown)
	{
		Stats->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(110)[ SNew(STextBlock).Font(Font(11)).Text(T(Label)) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Bar(200, 8, Col, [Value, Max]() { return FMath::Max(0.03f, Value / FMath::Max(Max, 0.01f)); }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0)[ SNew(STextBlock).Font(Font(11, TEXT("Bold"))).Text(T(Shown)) ]
		];
	};
	Stats->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Muted).Text(LOCTEXT("Attrs", "ATTRIBUTES")) ];
	const RPGJson::FObj A = RPGJson::Obj(C, TEXT("attributes"));
	for (const TCHAR* K : { TEXT("might"), TEXT("agility"), TEXT("focus"), TEXT("vitality"), TEXT("presence") })
	{
		FString Label = K; Label[0] = FChar::ToUpper(Label[0]);
		Row(Label, float(RPGJson::Num(A, K)), 10.f, Accent, FString::FromInt(int32(RPGJson::Num(A, K))));
	}
	TArray<FClassPreview> All;
	for (const FString& Id : ClassIds) All.Add(Preview(D, Id));
	const FClassPreview P = Preview(D, ClassId);
	auto Max = [&](float FClassPreview::* M) { float V = 0; for (const FClassPreview& X : All) V = FMath::Max(V, X.*M); return V; };
	Stats->AddSlot().AutoHeight().Padding(0, 10, 0, 4)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Muted).Text(LOCTEXT("Derived", "DERIVED STATS (LEVEL 1, STARTING GEAR)")) ];
	Row(TEXT("Max HP"), P.HP, Max(&FClassPreview::HP), FLinearColor(0.84f, 0.27f, 0.27f), FString::Printf(TEXT("%.0f"), P.HP));
	Row(TEXT("Stamina"), P.Stamina, Max(&FClassPreview::Stamina), FLinearColor(0.5f, 0.82f, 0.5f), FString::Printf(TEXT("%.0f"), P.Stamina));
	Row(TEXT("Mana"), P.Mana, Max(&FClassPreview::Mana), FLinearColor(0.31f, 0.5f, 0.88f), FString::Printf(TEXT("%.0f"), P.Mana));
	Row(TEXT("Move speed"), P.Speed, Max(&FClassPreview::Speed), FLinearColor(0.9f, 0.9f, 0.9f), FString::Printf(TEXT("%.0f"), P.Speed));
	Row(TEXT("Armor"), P.Armor, FMath::Max(1.f, Max(&FClassPreview::Armor)), Muted, FString::Printf(TEXT("%.0f"), P.Armor));
	Row(TEXT("Attack dmg"), P.Attack, Max(&FClassPreview::Attack), FLinearColor(1.f, 0.6f, 0.35f), FString::Printf(TEXT("%.1f"), P.Attack));
	Row(TEXT("Magic power"), P.Magic, Max(&FClassPreview::Magic), FLinearColor(0.7f, 0.55f, 1.f), FString::Printf(TEXT("x%.2f"), P.Magic));
	Row(TEXT("Crit chance"), P.Crit, Max(&FClassPreview::Crit), Gold, FString::Printf(TEXT("%.0f%%"), P.Crit));

	// Info column.
	auto Info = SNew(SVerticalBox);
	auto Section = [&](const FString& Head, const FString& Text, const FLinearColor& HeadCol)
	{
		Info->AddSlot().AutoHeight().Padding(0, 6, 0, 2)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(HeadCol).Text(T(Head)) ];
		Info->AddSlot().AutoHeight()[ SNew(STextBlock).Font(Font(11)).AutoWrapText(true).Text(T(Text)) ];
	};
	Section(TEXT("WEAPONS"), RPGJson::Str(C, TEXT("weapons")), Muted);
	Section(TEXT("DEFENSE"), RPGJson::Str(C, TEXT("defense")), Muted);
	const RPGJson::FObj Dlg = RPGJson::Obj(C, TEXT("dialogue"));
	Section(TEXT("DIALOGUE STYLE — ") + RPGJson::Str(Dlg, TEXT("style")).ToUpper(), RPGJson::Str(Dlg, TEXT("desc")), Accent);
	FString Abil;
	for (const auto& V : RPGJson::Arr(C, TEXT("abilities")))
	{
		const RPGJson::FObj Ab = D.Entry(TEXT("abilities"), V->AsString());
		Abil += FString::Printf(TEXT("Lv%d %s — %s\n"), int32(RPGJson::Num(Ab, TEXT("unlockLevel"), 1)), *RPGJson::Str(Ab, TEXT("name")), *RPGJson::Str(Ab, TEXT("desc")));
	}
	Section(TEXT("ABILITIES"), Abil.TrimEnd(), Muted);
	Info->AddSlot().AutoHeight().Padding(0, 8, 0, 4)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Muted).Text(LOCTEXT("Sex", "SEX")) ];
	Info->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
		[ SNew(SButton).HAlign(HAlign_Center).ButtonColorAndOpacity(Sex == TEXT("male") ? FLinearColor(0.25f, 0.28f, 0.36f) : FLinearColor(0.12f, 0.13f, 0.16f))
			.OnClicked_Lambda([this]() { SetSex(TEXT("male")); return FReply::Handled(); })[ SNew(STextBlock).Font(Font(12)).Text(LOCTEXT("Male", "Male (M)")) ] ]
		+ SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
		[ SNew(SButton).HAlign(HAlign_Center).ButtonColorAndOpacity(Sex == TEXT("female") ? FLinearColor(0.25f, 0.28f, 0.36f) : FLinearColor(0.12f, 0.13f, 0.16f))
			.OnClicked_Lambda([this]() { SetSex(TEXT("female")); return FReply::Handled(); })[ SNew(STextBlock).Font(Font(12)).Text(LOCTEXT("Female", "Female (F)")) ] ]
	];
	Info->AddSlot().AutoHeight().Padding(0, 6)
	[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted).AutoWrapText(true).Text(LOCTEXT("SexNote", "Stats are identical. Some NPCs react differently to you, opening different dialogue lines and occasional quest branches.")) ];

	Body->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(24, TEXT("Bold"))).Text(LOCTEXT("Choose", "Choose your hero")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).Text(LOCTEXT("ChooseHint", "Click a class (or press 1-4), pick a sex (M / F), then Begin (Enter).")) ]
		+ SVerticalBox::Slot().AutoHeight()[ Cards ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 12)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)[ Stats ]
			+ SHorizontalBox::Slot().FillWidth(1)[ Info ]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.3f, 0.25f, 0.1f)).ContentPadding(FMargin(20, 8))
			.OnClicked_Lambda([this]() { OnBegin.ExecuteIfBound(ClassId, Sex); return FReply::Handled(); })
			[ SNew(STextBlock).Font(Font(14, TEXT("Bold"))).ColorAndOpacity(Gold)
				.Text(T(FString::Printf(TEXT("Begin as %s %s"), Sex == TEXT("female") ? TEXT("Female") : TEXT("Male"), *RPGJson::Str(C, TEXT("name"))))) ]
		]
	);
}

FReply SRPGCharSelect::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
	for (int32 I = 0; I < 4 && I < ClassIds.Num(); ++I) if (E.GetKey() == Digits[I]) { Select(ClassIds[I]); return FReply::Handled(); }
	if (E.GetKey() == EKeys::M) { SetSex(TEXT("male")); return FReply::Handled(); }
	if (E.GetKey() == EKeys::F) { SetSex(TEXT("female")); return FReply::Handled(); }
	if (E.GetKey() == EKeys::Enter) { OnBegin.ExecuteIfBound(ClassId, Sex); return FReply::Handled(); }
	return FReply::Unhandled();
}

// =============================================================================================
// Panels: Inventory / Character / Quests / Help
// =============================================================================================

void SRPGPanel::Construct(const FArguments& Args)
{
	World = Args._World;
	OnClose = Args._OnClose;
	ChildSlot
	.HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).MinDesiredWidth(560).MaxDesiredWidth(900)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(PanelColor).Padding(FMargin(24, 18))
			[ SAssignNew(Body, SBox) ]
		]
	];
}

void SRPGPanel::Show(FName InMode)
{
	Mode = InMode;
	Rebuild();
}

void SRPGPanel::Rebuild()
{
	ARPGPlayerCharacter* P = PlayerOf(World);
	URPGStory* S = StoryOf(World);
	if (!P || !S) return;
	const URPGData& D = URPGData::Get(World.Get());
	auto V = SNew(SVerticalBox);
	auto Head = [&](const FString& Title, const FString& Right = FString())
	{
		V->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[ SNew(STextBlock).Font(Font(20, TEXT("Bold"))).Text(T(Title)) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(STextBlock).Font(Font(13, TEXT("Bold"))).ColorAndOpacity(Gold).Text(T(Right)) ]
		];
	};
	auto Line = [&](const FString& Text, const FLinearColor& C = FLinearColor(0.9f, 0.9f, 0.9f), int32 Size = 12)
	{
		V->AddSlot().AutoHeight().Padding(0, 2)[ SNew(STextBlock).Font(Font(Size)).ColorAndOpacity(C).AutoWrapText(true).Text(T(Text)) ];
	};

	if (Mode == TEXT("Help"))
	{
		Head(TEXT("Controls"));
		for (const TCHAR* L : {
			TEXT("WASD — move        Mouse — look / aim"),
			TEXT("Left click — attack (hold to keep swinging). Mage: arcane bolt"),
			TEXT("Right click (hold) — block (Knight, Scholar) / draw bow, release to fire (Thief)"),
			TEXT("Space — jump        Left Shift — dodge roll (invulnerable, costs stamina)"),
			TEXT("1-4 — class abilities (unlock at levels 1-4)      Q — drink potion"),
			TEXT("E — talk      X — Knight: switch Sword & Shield / Longsword"),
			TEXT("I / C / J / H — inventory / character / quests / this help      Esc — close"),
			TEXT("~ — debug (shows dialogue odds, enemy states). While on: K = level up, G = +100 gold"),
		}) Line(L);
		Line(TEXT("Tip: talk to Elder Maren (the ! above her head) to begin."), Muted, 11);
	}
	else if (Mode == TEXT("Quests"))
	{
		Head(TEXT("Quest Log"));
		if (S->Quests.IsEmpty()) Line(TEXT("No quests yet. Look for a ! above someone's head."), Muted);
		for (const auto& KV : S->Quests)
		{
			const RPGJson::FObj Q = D.Entry(TEXT("quests"), KV.Key);
			const FString St = KV.Value.Status;
			Line(RPGJson::Str(Q, TEXT("name")), St == TEXT("turnedIn") ? Muted : Gold, 14);
			Line(RPGJson::Str(Q, TEXT("desc")), Muted, 11);
			Line(St == TEXT("turnedIn") ? TEXT("Completed") : St == TEXT("complete") ? TEXT("Return to ") + RPGJson::Str(Q, TEXT("giver")) : TEXT("Progress: ") + S->ProgressText(KV.Key));
		}
	}
	else if (Mode == TEXT("Character"))
	{
		Head(TEXT("Character — ") + FString(P->Sex == TEXT("female") ? TEXT("Female ") : TEXT("Male ")) + P->DisplayName, P->AttrPoints ? FString::Printf(TEXT("%d points to spend"), P->AttrPoints) : FString());
		TWeakObjectPtr<UWorld> W = World;
		for (const TCHAR* K : { TEXT("might"), TEXT("agility"), TEXT("focus"), TEXT("vitality"), TEXT("presence") })
		{
			const FName Stat(K);
			FString Label = K; Label[0] = FChar::ToUpper(Label[0]);
			const float Total = P->Stats->Get(Stat), Bonus = Total - P->Stats->Base.FindRef(Stat);
			V->AddSlot().AutoHeight().Padding(0, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(120)[ SNew(STextBlock).Font(Font(13)).Text(T(Label)) ] ]
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(50)[ SNew(STextBlock).Font(Font(13, TEXT("Bold"))).Text(T(FString::Printf(TEXT("%.0f"), Total))) ] ]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).IsEnabled(P->AttrPoints > 0).OnClicked_Lambda([this, W, Stat]() {
						ARPGPlayerCharacter* PP = PlayerOf(W);
						if (PP && PP->AttrPoints > 0)
						{
							PP->Stats->Base.FindOrAdd(Stat) += 1.f;
							--PP->AttrPoints;
							if (Stat == TEXT("vitality")) PP->Stats->HP += float(URPGData::Get(W.Get()).Tuning(TEXT("hpPerVitality"), 10));
						}
						Rebuild();
						return FReply::Handled(); })
					[ SNew(STextBlock).Font(Font(12, TEXT("Bold"))).Text(LOCTEXT("Plus", " + ")) ]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).Text(T(Bonus != 0 ? FString::Printf(TEXT("(%+.0f gear)"), Bonus) : FString())) ]
			];
		}
		const URPGStatsComponent* St = P->Stats;
		const float K = float(D.Tuning(TEXT("armorConstant"), 100));
		Line(TEXT(""));
		Line(FString::Printf(TEXT("Level %d   ·   XP %d / %d"), P->Level(), P->Xp, ARPGPlayerCharacter::XpToNext(P, P->Level())));
		Line(FString::Printf(TEXT("Max HP %.0f   ·   Stamina %.0f   ·   Mana %.0f"), St->MaxHP(), St->MaxStamina(), St->MaxMana()));
		Line(FString::Printf(TEXT("Crit %.0f%%   ·   Armor %.0f (-%.0f%% damage)   ·   Weapon damage %.0f"), St->CritChance() * 100, St->Armor(), (1 - K / (K + St->Armor())) * 100, St->Get(TEXT("weaponDamage"))));
		Line(FString::Printf(TEXT("Weapon style: %s"), *RPGJson::Str(P->Style(), TEXT("name"))), Muted, 11);
	}
	else if (Mode == TEXT("Inventory"))
	{
		Head(TEXT("Inventory"), FString::Printf(TEXT("%d gold"), P->Inventory->Gold));
		TWeakObjectPtr<UWorld> W = World;
		auto ItemTip = [&](const FRPGItem& It)
		{
			FString Tip = It.Name + TEXT(" (") + It.Rarity + TEXT(")");
			for (const auto& M : It.Mods) Tip += FString::Printf(TEXT("\n%+.0f %s"), M.Value, *M.Key.ToString());
			if (It.Type == TEXT("consumable")) Tip += TEXT("\nRestores health");
			return Tip;
		};
		Line(TEXT("EQUIPPED"), Muted, 10);
		auto Eq = SNew(SHorizontalBox);
		for (const TCHAR* Slot : { TEXT("weapon"), TEXT("armor"), TEXT("trinket") })
		{
			const FRPGItem* It = P->Inventory->Equipment.Find(Slot);
			const FString SlotName = Slot;
			Eq->AddSlot().FillWidth(1).Padding(3)
			[
				SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.12f, 0.13f, 0.16f)).ToolTipText(T(It ? ItemTip(*It) : FString()))
				.OnClicked_Lambda([this, W, SlotName]() { if (ARPGPlayerCharacter* PP = PlayerOf(W)) PP->Inventory->Unequip(SlotName); Rebuild(); return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Muted).Text(T(SlotName.ToUpper())) ]
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(It ? URPGInventoryComponent::RarityColor(P, It->Rarity) : Muted).Text(T(It ? It->Name : TEXT("—"))) ]
				]
			];
		}
		V->AddSlot().AutoHeight()[ Eq ];
		Line(TEXT("BAG"), Muted, 10);
		auto Bag = SNew(SWrapBox).PreferredSize(820);
		for (const FRPGItem& It : P->Inventory->Items)
		{
			const int32 Uid = It.Uid;
			Bag->AddSlot().Padding(3)
			[
				SNew(SBox).WidthOverride(150).HeightOverride(54)
				[
					SNew(SButton).ButtonColorAndOpacity(FLinearColor(0.12f, 0.13f, 0.16f)).ToolTipText(T(ItemTip(It)))
					.OnClicked_Lambda([this, W, Uid]() {
						if (ARPGPlayerCharacter* PP = PlayerOf(W))
						{
							if (FSlateApplication::Get().GetModifierKeys().IsShiftDown()) PP->Inventory->Salvage(Uid);
							else PP->Inventory->Use(Uid);
						}
						Rebuild();
						return FReply::Handled(); })
					[ SNew(STextBlock).Font(Font(11)).AutoWrapText(true).ColorAndOpacity(URPGInventoryComponent::RarityColor(P, It.Rarity))
						.Text(T(It.Qty > 1 ? FString::Printf(TEXT("%s  x%d"), *It.Name, It.Qty) : It.Name)) ]
				]
			];
		}
		V->AddSlot().AutoHeight()[ Bag ];
		Line(TEXT("Click: equip / use   ·   Click equipped: unequip   ·   Shift+Click: salvage for gold   ·   Hover for stats"), Muted, 10);
	}
	V->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0, 12, 0, 0)
	[ SNew(SButton).OnClicked_Lambda([this]() { OnClose.ExecuteIfBound(); return FReply::Handled(); })[ SNew(STextBlock).Font(Font(12)).Text(LOCTEXT("Close", "Close (Esc)")) ] ];
	Body->SetContent(V);
}

void SRPGPanel::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SCompoundWidget::Tick(G, Time, Dt);
	if (GetVisibility() == EVisibility::Visible && !HasKeyboardFocus() && !HasFocusedDescendants()) FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
}

FReply SRPGPanel::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (K == EKeys::Escape || (K == EKeys::I && Mode == TEXT("Inventory")) || (K == EKeys::C && Mode == TEXT("Character")) ||
		(K == EKeys::J && Mode == TEXT("Quests")) || (K == EKeys::H && Mode == TEXT("Help")))
	{
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
