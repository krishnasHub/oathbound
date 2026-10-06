#include "SRPGWidgets.h"
#include "RPGSprite.h"
#include "Widgets/Layout/SScaleBox.h"
#include "RPGPlayerController.h"
#include "RPGWorldBuilder.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/TransformCalculus2D.h"
#include "Engine/Texture2D.h"
#include "RPGAssets.h"
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

	// Ability picker (Shift + wheel): the four abilities over a translucent backdrop, the highlighted one
	// in gold with its description. The world runs in slow motion while it's open.
	auto PickDef = [W](int32 I) -> RPGJson::FObj { const ARPGPlayerCharacter* P = PlayerOf(W); return P && P->Abilities->Ids.IsValidIndex(I) ? P->Abilities->Def(P->Abilities->Ids[I]) : nullptr; };
	auto PickCards = SNew(SHorizontalBox);
	for (int32 I = 0; I < 4; ++I)
	{
		PickCards->AddSlot().AutoWidth().Padding(6, 0)
		[
			SNew(SBorder).BorderImage(White()).Padding(3)
			.BorderBackgroundColor_Lambda([W, I]() { const ARPGPlayerCharacter* P = PlayerOf(W);
				return FSlateColor(P && P->PickerSlot() == I ? Gold : FLinearColor(1, 1, 1, 0.12f)); })
			[
				SNew(SBox).WidthOverride_Lambda([W, I]() { const ARPGPlayerCharacter* P = PlayerOf(W); return FOptionalSize(P && P->PickerSlot() == I ? 132.f : 112.f); })
				.HeightOverride_Lambda([W, I]() { const ARPGPlayerCharacter* P = PlayerOf(W); return FOptionalSize(P && P->PickerSlot() == I ? 96.f : 80.f); })
				[
					SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.08f, 0.92f)).Padding(6)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Justification(ETextJustify::Center).AutoWrapText(true)
						.Font_Lambda([W, I]() { const ARPGPlayerCharacter* P = PlayerOf(W); return Font(P && P->PickerSlot() == I ? 14 : 11, TEXT("Bold")); })
						.Text_Lambda([W, I, PickDef]() { const ARPGPlayerCharacter* P = PlayerOf(W); const RPGJson::FObj D = PickDef(I);
							if (!P || !D) return FText::GetEmpty();
							const FString Id = P->Abilities->Ids[I];
							if (!P->Abilities->Unlocked(Id)) return T(FString::Printf(TEXT("%d  %s\nLv %d"), I + 1, *RPGJson::Str(D, TEXT("name")), int32(RPGJson::Num(D, TEXT("unlockLevel"), 1))));
							const float Cd = P->Abilities->Cooldowns.FindRef(Id);
							return T(FString::Printf(TEXT("%d  %s%s"), I + 1, *RPGJson::Str(D, TEXT("name")), Cd > 0.f ? *FString::Printf(TEXT("\n%.1fs"), Cd) : TEXT(""))); })
						.ColorAndOpacity_Lambda([W, I, PickDef]() { const ARPGPlayerCharacter* P = PlayerOf(W); const RPGJson::FObj D = PickDef(I);
							if (!P || !D || !P->Abilities->Unlocked(P->Abilities->Ids[I])) return FSlateColor(FLinearColor(0.45f, 0.45f, 0.48f));
							return FSlateColor(RPGJson::Color(RPGJson::Str(D, TEXT("color")))); })
					]
				]
			]
		];
	}
	auto Picker = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f)).Padding(FMargin(28, 18))
		.Visibility_Lambda([W]() { const ARPGPlayerCharacter* P = PlayerOf(W); return P && P->PickerSlot() >= 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
			[ SNew(STextBlock).Font(Font(12, TEXT("Bold"))).ColorAndOpacity(Muted).Text(T(TEXT("CHOOSE AN ABILITY"))) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ PickCards ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 14, 0, 2)
			[
				SNew(STextBlock).Font(Font(16, TEXT("Bold"))).ColorAndOpacity(Gold)
				.Text_Lambda([W, PickDef]() { const ARPGPlayerCharacter* P = PlayerOf(W); const RPGJson::FObj D = P ? PickDef(P->PickerSlot()) : nullptr; if (!D) return FText::GetEmpty();
					const double Mana = RPGJson::Num(D, TEXT("mana"), 0), Sta = RPGJson::Num(D, TEXT("stamina"), 0);
					return T(RPGJson::Str(D, TEXT("name")) + (Mana > 0 ? FString::Printf(TEXT("   ·   %.0f mana"), Mana) : Sta > 0 ? FString::Printf(TEXT("   ·   %.0f stamina"), Sta) : FString())
						+ FString::Printf(TEXT("   ·   %.0fs cooldown"), RPGJson::Num(D, TEXT("cooldown"), 0))); })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(560)
				[
					SNew(STextBlock).Font(Font(12)).AutoWrapText(true).Justification(ETextJustify::Center).ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.88f))
					.Text_Lambda([W, PickDef]() { const ARPGPlayerCharacter* P = PlayerOf(W); const RPGJson::FObj D = P ? PickDef(P->PickerSlot()) : nullptr; return D ? T(RPGJson::Str(D, TEXT("desc"))) : FText::GetEmpty(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 12, 0, 0)
			[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted).Text(T(TEXT("scroll to change   ·   release Shift or click to cast at the cursor   ·   RMB to cancel"))) ]
		];

	// Low health: a red vignette creeps in below 40% health, deeper and pulsing faster the closer to death.
	if (UTexture2D* Vig = RPGAssets::Load<UTexture2D>(TEXT("/Game/RPG/Pixel/UI_Vignette.UI_Vignette")))
	{
		VignetteTex.Reset(Vig);
		VignetteBrush.SetResourceObject(Vig);
		VignetteBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	auto LowHealth = SNew(SImage).Image(&VignetteBrush)
		.ColorAndOpacity_Lambda([W]() {
			const ARPGPlayerCharacter* P = PlayerOf(W);
			if (!P || P->IsDead()) return FSlateColor(FLinearColor(0.7f, 0.02f, 0.02f, 0.f));
			const float Frac = P->Stats->HP / FMath::Max(1.f, P->Stats->MaxHP());
			const float K = FMath::Clamp((0.4f - Frac) / 0.4f, 0.f, 1.f);
			if (K <= 0.f) return FSlateColor(FLinearColor(0.7f, 0.02f, 0.02f, 0.f));
			const float T = W.IsValid() ? W->GetRealTimeSeconds() : 0.f;
			const float Pulse = 0.82f + 0.18f * FMath::Sin(T * (3.f + 6.f * K));
			return FSlateColor(FLinearColor(0.7f, 0.02f, 0.02f, (0.3f + 0.6f * K) * Pulse)); });

	ChildSlot
	[
		SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
		+ SOverlay::Slot()[ SNew(SRPGNightShade).World(W) ]
		+ SOverlay::Slot()[ LowHealth ]
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
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 22, 22)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 4)
			[
				SNew(STextBlock).Font(Font(13, TEXT("Bold"))).ColorAndOpacity(FLinearColor(0.95f, 0.92f, 0.82f)).ShadowOffset(FVector2D(1, 1))
				.Text_Lambda([]() { return T(ARPGWorldBuilder::ClockText()); })
			]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(SRPGMinimap).World(W) ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 170)[ Picker ]
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
// Game cursor
// =============================================================================================

void SRPGCursor::Construct(const FArguments& Args)
{
	World = Args._World;
	SetVisibility(EVisibility::HitTestInvisible);
	for (const TCHAR* N : { TEXT("pointer"), TEXT("sword"), TEXT("dagger"), TEXT("wand"), TEXT("arrow"), TEXT("talk"), TEXT("talk_off") })
	{
		UTexture2D* Tex = RPGAssets::Load<UTexture2D>(RPGAssets::ObjPath(TEXT("/Game/RPG/Pixel"), FString(TEXT("CUR_")) + N));
		if (!Tex) continue;
		Keep.Add(TStrongObjectPtr<UTexture2D>(Tex));
		FSlateBrush& B = Brushes.Add(N);
		B.SetResourceObject(Tex);
		B.ImageSize = FVector2D(40, 40);
		B.DrawAs = ESlateBrushDrawType::Image;
	}
}

int32 SRPGCursor::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const APlayerController* PC = World.IsValid() ? World->GetFirstPlayerController() : nullptr;
	const ARPGPlayerController* RPC = Cast<ARPGPlayerController>(PC);
	const ARPGPlayerCharacter* P = PlayerOf(World);
	if (!RPC || !P || !RPC->IsInGameplay() || !FSlateApplication::IsInitialized()) return Layer;
	const FName Icon = P->CursorIcon();
	const FSlateBrush* B = Brushes.Find(Icon);
	if (!B) return Layer;
	// The talk bubble is centred on the point; weapons and the pointer have their tip there.
	const bool bCentre = Icon == TEXT("talk") || Icon == TEXT("talk_off");
	const FVector2D At = G.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos()) - (bCentre ? FVector2D(20, 20) : FVector2D(2, 2));
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2D(40, 40), FSlateLayoutTransform(At)), B, ESlateDrawEffect::None, FLinearColor::White);
	return Layer + 1;
}

// =============================================================================================
// Night shade
// =============================================================================================

void SRPGNightShade::Construct(const FArguments& Args)
{
	World = Args._World;
	if (UMaterialInterface* Base = RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_NightShade.M_RPG_NightShade")))
		Mat.Reset(UMaterialInstanceDynamic::Create(Base, GetTransientPackage()));
	Brush.SetResourceObject(Mat.Get());
	Brush.DrawAs = ESlateBrushDrawType::Image;
}

void SRPGNightShade::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SLeafWidget::Tick(G, Time, Dt);
	Strength = ARPGWorldBuilder::Darkness();
	const ARPGPlayerCharacter* P = PlayerOf(World);
	APlayerController* PC = World.IsValid() ? World->GetFirstPlayerController() : nullptr;
	if (!Mat || !P || !PC || Strength < 0.001f || !GEngine || !GEngine->GameViewport) return;
	FVector2D Vp;
	GEngine->GameViewport->GetViewportSize(Vp);
	if (Vp.X < 1.f || Vp.Y < 1.f) return;

	// A world circle (centre, radius) as a screen ellipse in 0..1 UV: project the centre and a point east and north.
	const float Z = P->GetActorLocation().Z;
	auto Ellipse = [&](const FVector& At, float R) -> FLinearColor
	{
		FVector2D S0, SX, SY;
		const FVector C(At.X, At.Y, Z);
		if (!PC->ProjectWorldLocationToScreen(C, S0) || !PC->ProjectWorldLocationToScreen(C + FVector(R, 0, 0), SX) || !PC->ProjectWorldLocationToScreen(C + FVector(0, -R, 0), SY))
			return FLinearColor(0, 0, 0, 0);
		return FLinearColor(S0.X / Vp.X, S0.Y / Vp.Y, FMath::Max(FVector2D::Distance(S0, SX) / Vp.X, 0.001f), FMath::Max(FVector2D::Distance(S0, SY) / Vp.Y, 0.001f));
	};
	Mat->SetScalarParameterValue(TEXT("Night"), Strength);
	Mat->SetVectorParameterValue(TEXT("Hero"), Ellipse(P->GetActorLocation(), ARPGWorldBuilder::HeroSight()));
	const FVector At = P->GetActorLocation();
	TArray<FVector> Lights = ARPGWorldBuilder::NightLights();
	Lights.Sort([&At](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A, At) < FVector::DistSquared2D(B, At); });
	for (int32 I = 0; I < 8; ++I)
		Mat->SetVectorParameterValue(*FString::Printf(TEXT("Light%d"), I), Lights.IsValidIndex(I) ? Ellipse(FVector(Lights[I].X, Lights[I].Y, 0), Lights[I].Z) : FLinearColor(0, 0, 0, 0));
}

int32 SRPGNightShade::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (Strength >= 0.001f && Mat) FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), &Brush, ESlateDrawEffect::None, FLinearColor::White);
	return Layer + 1;
}

// =============================================================================================
// Minimap
// =============================================================================================

namespace
{
	constexpr int32 MiniPad = 6;   // forest tiles around the map in MAP_Mini (tools/pixelart/build_all.py PAD)
}

void SRPGMinimap::Construct(const FArguments& Args)
{
	World = Args._World;
	if (UMaterialInterface* Base = RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Minimap.M_RPG_Minimap")))
		Mat.Reset(UMaterialInstanceDynamic::Create(Base, GetTransientPackage()));
	MapBrush.SetResourceObject(Mat.Get());
	MapBrush.ImageSize = FVector2D(Size);
	MapBrush.DrawAs = ESlateBrushDrawType::Image;
	SetShape(TEXT("orb"));
}

void SRPGMinimap::SetShape(const FString& InShape)
{
	if (InShape == Shape) return;
	Shape = InShape;
	FrameTex.Reset(RPGAssets::Load<UTexture2D>(RPGAssets::ObjPath(TEXT("/Game/RPG/Pixel"), TEXT("MM_Frame_") + Shape)));
	MaskTex.Reset(RPGAssets::Load<UTexture2D>(RPGAssets::ObjPath(TEXT("/Game/RPG/Pixel"), TEXT("MM_Mask_") + Shape)));
	FrameBrush = FSlateBrush();
	FrameBrush.SetResourceObject(FrameTex.Get());
	FrameBrush.ImageSize = FVector2D(Size);
	FrameBrush.DrawAs = ESlateBrushDrawType::Image;
	if (Mat && MaskTex) Mat->SetTextureParameterValue(TEXT("Mask"), MaskTex.Get());
}

bool SRPGMinimap::Inside(const FVector2D& P) const
{
	if (Shape == TEXT("shield"))
	{
		const float W = P.Y <= 0.05f ? 0.8f : 0.8f * FMath::Pow(FMath::Clamp(1.f - (P.Y - 0.05f) / 0.8f, 0.f, 1.f), 0.55f);
		return P.Y >= -0.76f && FMath::Abs(P.X) <= W;
	}
	if (Shape == TEXT("book")) return FMath::Abs(P.X) <= 0.82f && FMath::Abs(P.Y) <= 0.7f;
	return P.Size() <= 0.82f;   // orb, coin
}

void SRPGMinimap::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SLeafWidget::Tick(G, Time, Dt);
	const ARPGPlayerCharacter* P = PlayerOf(World);
	if (!P || !Mat) return;
	static const TMap<FString, FString> Shapes = { { TEXT("mage"), TEXT("orb") }, { TEXT("knight"), TEXT("shield") }, { TEXT("thief"), TEXT("coin") }, { TEXT("scholar"), TEXT("book") } };
	if (const FString* Sh = Shapes.Find(P->ClassId)) SetShape(*Sh);
	const URPGData& D = URPGData::Get(World.Get());
	PlayerAt = P->GetActorLocation();
	const float TilesW = float(D.MapW + 2 * MiniPad), TilesH = float(D.MapH + 2 * MiniPad);
	Mat->SetVectorParameterValue(TEXT("Center"), FLinearColor((PlayerAt.X / D.TileSize + MiniPad) / TilesW, (PlayerAt.Y / D.TileSize + MiniPad) / TilesH, 0, 0));
	Mat->SetVectorParameterValue(TEXT("Span"), FLinearColor(ViewTiles / TilesW, ViewTiles / TilesH, 0, 0));
}

int32 SRPGMinimap::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	const FVector2D Sz = G.GetLocalSize();
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), &MapBrush, ESlateDrawEffect::None, FLinearColor::White);

	const URPGData& D = URPGData::Get(World.Get());
	const ARPGPlayerCharacter* Player = PlayerOf(World);
	const URPGStory* Story = StoryOf(World);
	auto Dot = [&](const FVector& WorldAt, float Px, const FLinearColor& Col, int32 L)
	{
		const FVector2D N((WorldAt.X - PlayerAt.X) / D.TileSize / ViewTiles * 2.f, (WorldAt.Y - PlayerAt.Y) / D.TileSize / ViewTiles * 2.f);
		if (!Inside(N)) return;
		const FVector2D C = Sz * 0.5f + N * Sz * 0.5f;
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2D(Px + 2.f, Px + 2.f), FSlateLayoutTransform(C - FVector2D((Px + 2.f) * 0.5f))), White(), ESlateDrawEffect::None, FLinearColor(0.03f, 0.02f, 0.04f));
		FSlateDrawElement::MakeBox(Out, L + 1, G.ToPaintGeometry(FVector2D(Px, Px), FSlateLayoutTransform(C - FVector2D(Px * 0.5f))), White(), ESlateDrawEffect::None, Col);
	};
	if (World.IsValid())
	{
		for (TActorIterator<ARPGCharacterBase> It(World.Get()); It; ++It)
		{
			const ARPGCharacterBase* C = *It;
			if (C == Player || C->IsDead() || C->IsHidden()) continue;
			if (C->Team == ERPGTeam::Enemy && !ARPGWorldBuilder::IsLit(C->GetActorLocation(), PlayerAt)) continue;   // foes hide in the dark
			const bool bMarker = Story && !Story->MarkerFor(C).IsEmpty();
			const FLinearColor Col = bMarker ? Gold : C->Team == ERPGTeam::Villager ? FLinearColor(0.95f, 0.85f, 0.35f)
				: C->IsPassive() ? FLinearColor(1.f, 0.55f, 0.15f) : FLinearColor(0.95f, 0.18f, 0.15f);
			Dot(C->GetActorLocation(), bMarker ? 9.f : 6.f, Col, Layer + 1);
		}
	}
	if (Player)
	{
		Dot(PlayerAt, 9.f, FLinearColor::White, Layer + 3);
		Dot(PlayerAt + Player->Facing() * D.TileSize * 0.75f, 4.f, FLinearColor::White, Layer + 3);   // which way you face
	}
	FSlateDrawElement::MakeBox(Out, Layer + 5, G.ToPaintGeometry(), &FrameBrush, ESlateDrawEffect::None, FLinearColor::White);
	return Layer + 6;
}

// =============================================================================================
// Dialogue
// =============================================================================================

void SRPGDialogue::Construct(const FArguments& Args)
{
	World = Args._World;
	if (UTexture2D* Fade = RPGAssets::Load<UTexture2D>(TEXT("/Game/RPG/Pixel/UI_FadeRadial.UI_FadeRadial")))
	{
		FadeTex.Reset(Fade);
		FadeBrush.SetResourceObject(Fade);
		FadeBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	TWeakObjectPtr<UWorld> W = World;
	constexpr float PortraitH = 580.f;
	auto Portrait = [](FSlateBrush* Brush) -> TSharedRef<SWidget>
	{
		return SNew(SBox).HeightOverride(PortraitH).WidthOverride(PortraitH * 1.1f)
			[ SNew(SScaleBox).Stretch(EStretch::ScaleToFit).VAlign(VAlign_Bottom)[ SNew(SImage).Image(Brush) ] ];
	};
	ChildSlot
	[
		SNew(SOverlay)
		// The world, dimmed and paused behind the conversation.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(10, 0, 0, 0)
		[ SAssignNew(NpcPortrait, SBox).Visibility(EVisibility::HitTestInvisible)[ Portrait(&NpcBrush) ] ]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 10, 0)
		[ SAssignNew(HeroPortrait, SBox).Visibility(EVisibility::HitTestInvisible).RenderTransformPivot(FVector2D(0.5f, 0.5f))[ Portrait(&HeroBrush) ] ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 140)
		[
			SAssignNew(DialogBox, SBox).WidthOverride(820)
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
		]
	];
}

void SRPGDialogue::SetPortrait(FSlateBrush& Brush, TStrongObjectPtr<UTexture2D>& Keep, const ARPGCharacterBase* Who)
{
	UTexture2D* Tex = nullptr;
	if (Who && Who->Sprite)
		Tex = LoadObject<UTexture2D>(nullptr, *RPGAssets::ObjPath(TEXT("/Game/RPG/Pixel"), TEXT("POR_") + Who->Sprite->SheetName));
	Keep.Reset(Tex);
	Brush = FSlateBrush();
	Brush.DrawAs = Tex ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
	if (Tex)
	{
		Brush.SetResourceObject(Tex);
		Brush.ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
	}
}

void SRPGDialogue::Refresh()
{
	Choices->ClearChildren();
	URPGStory* S = StoryOf(World);
	if (!S) return;
	if (!Fx.Busy()) Fx.Reset();   // a new line fades in
	if (!bWasOpen)   // just opened: who's talking, and slide the portraits in
	{
		// Portraits are off until there's proper illustrated art (world3d.dialoguePortraits).
		const bool bPortraits = RPGJson::Bool(URPGData::Get(World.Get()).World3D(), TEXT("dialoguePortraits"), false);
		SetPortrait(NpcBrush, NpcTex, bPortraits ? S->DialogueNpc.Get() : nullptr);
		SetPortrait(HeroBrush, HeroTex, bPortraits ? S->Player() : nullptr);
		Appear = 0.f;
	}
	bWasOpen = true;
	Highlight = -1;
	MoveHighlight(1);   // first enabled choice
	for (int32 I = 0; I < S->ChoiceViews.Num(); ++I)
	{
		const FRPGChoiceView& V = S->ChoiceViews[I];
		TWeakObjectPtr<UWorld> W = World;
		Choices->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SButton).IsFocusable(false).IsEnabled(V.bEnabled)
			.ButtonColorAndOpacity_Lambda([this, I]() { return Fx.Color(I, Highlight); })
			.OnHovered_Lambda([this, I, bOn = V.bEnabled]() { if (bOn && !Fx.Busy()) Highlight = I; })
			.OnClicked_Lambda([this, I]() { Confirm(I); return FReply::Handled(); })
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

int32 SRPGDialogue::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	// Behind the conversation, the world darkens around the dialogue box and fades back to clear away from it.
	if (DialogBox && FadeTex)
	{
		const FGeometry& BG = DialogBox->GetPaintSpaceGeometry();
		const FVector2D Size = BG.GetLocalSize();
		if (Size.X > 1.f)
		{
			const FVector2D Centre = G.AbsoluteToLocal(BG.LocalToAbsolute(Size * 0.5f));
			const FVector2D FadeSize(Size.X * 2.6f, Size.Y * 4.2f);
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FadeSize, FSlateLayoutTransform(Centre - FadeSize * 0.5f)), &FadeBrush,
				ESlateDrawEffect::None, FLinearColor(1, 1, 1, FadeIn));
		}
	}
	return SCompoundWidget::OnPaint(Args, G, Cull, Out, Layer + 1, Style, bParentEnabled);
}

FVector2D SRPGDialogue::ChoiceScreenCenter(int32 I) const
{
	FChildren* Kids = Choices->GetChildren();
	if (!Kids || I < 0 || I >= Kids->Num()) return FVector2D::ZeroVector;
	return Kids->GetChildAt(I)->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
}

void SRPGDialogue::Confirm(int32 Index)
{
	URPGStory* S = StoryOf(World);
	if (!S || Fx.Busy() || !S->ChoiceViews.IsValidIndex(Index) || !S->ChoiceViews[Index].bEnabled) return;
	Highlight = Index;
	TWeakObjectPtr<UWorld> W = World;
	Fx.Start(Index, [W, Index]() { if (URPGStory* St = StoryOf(W)) St->Choose(Index); });
}

void SRPGDialogue::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SCompoundWidget::Tick(G, Time, Dt);
	SetRenderOpacity(Fx.Tick(Dt));
	const URPGStory* Story = StoryOf(World);
	if (!Story || !Story->IsDialogueOpen()) bWasOpen = false;
	Appear = FMath::Min(1.f, Appear + Dt * 4.f);
	const float Ease = 1.f - FMath::Square(1.f - Appear);
	const float Rise = (1.f - Ease) * 220.f;
	FadeIn = Ease;
	if (NpcPortrait) { NpcPortrait->SetRenderTransform(FSlateRenderTransform(FVector2D(-Rise * 0.5f, Rise))); NpcPortrait->SetRenderOpacity(Ease); }
	if (HeroPortrait)   // mirrored so the hero faces the person they're talking to
	{
		HeroPortrait->SetRenderTransform(FSlateRenderTransform(FScale2D(-1.f, 1.f), FVector2D(Rise * 0.5f, Rise)));
		HeroPortrait->SetRenderOpacity(Ease);
	}
	// Modal: if a click (or anything else) took keyboard focus away, take it back so 1-9 / Esc keep working.
	const URPGStory* S = StoryOf(World);
	if (S && S->IsDialogueOpen() && !HasKeyboardFocus()) FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
}

FReply SRPGDialogue::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	URPGStory* S = StoryOf(World);
	if (!S || !S->IsDialogueOpen()) return FReply::Unhandled();
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	if (Fx.Busy()) return FReply::Handled();
	for (int32 I = 0; I < 9; ++I) if (E.GetKey() == Digits[I]) { Confirm(I); return FReply::Handled(); }
	const FKey K = E.GetKey();
	if (K == EKeys::Up || K == EKeys::W || K == EKeys::Gamepad_DPad_Up) { MoveHighlight(-1); return FReply::Handled(); }
	if (K == EKeys::Down || K == EKeys::S || K == EKeys::Gamepad_DPad_Down) { MoveHighlight(1); return FReply::Handled(); }
	if ((K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::E || K == EKeys::Gamepad_FaceButton_Bottom) && S->ChoiceViews.IsValidIndex(Highlight))
	{
		Confirm(Highlight);
		return FReply::Handled();
	}
	if (K == EKeys::Escape || K == EKeys::Gamepad_FaceButton_Right) { S->CloseDialogue(); return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SRPGDialogue::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	const URPGStory* S = StoryOf(World);
	if (!S || !S->IsDialogueOpen()) return FReply::Unhandled();
	MoveHighlight(E.GetWheelDelta() > 0.f ? -1 : 1);
	return FReply::Handled();
}

void SRPGDialogue::MoveHighlight(int32 Step)
{
	const URPGStory* S = StoryOf(World);
	const int32 N = S ? S->ChoiceViews.Num() : 0;
	for (int32 I = 1; I <= N; ++I)
	{
		const int32 C = ((Highlight + Step * I) % N + N) % N;   // wraps around
		if (S->ChoiceViews[C].bEnabled) { Highlight = C; return; }
	}
}

// =============================================================================================
// Character select
// =============================================================================================

// =============================================================================================
// Character-select backdrop
// =============================================================================================

namespace
{
	constexpr float BDLoop = 14.f;
	float Ramp(float T, float A, float B) { return FMath::Clamp((T - A) / FMath::Max(B - A, 0.001f), 0.f, 1.f); }
	float Hash(int32 I, int32 Salt = 0) { return FMath::Frac(FMath::Sin(float(I * 127 + Salt * 311) * 12.9898f) * 43758.5453f); }
}

void SRPGBackdrop::Construct(const FArguments& Args)
{
	Hero = Args._Hero;
	SetVisibility(EVisibility::HitTestInvisible);
	SetClipping(EWidgetClipping::ClipToBounds);   // the scene is wider than the column
}

void SRPGBackdrop::SetScene(const FString& ClassId)
{
	if (Scene != ClassId) T = 0.f;
	Scene = ClassId;
}

void SRPGBackdrop::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SLeafWidget::Tick(G, Time, Dt);
	T = FMath::Fmod(T + Dt, BDLoop * 1000.f);
}

const FSlateBrush* SRPGBackdrop::Brush(const FString& Texture, int32 Frames, int32 Frame) const
{
	TArray<FSlateBrush>* Set = Brushes.Find(Texture);
	if (!Set)
	{
		Set = &Brushes.Add(Texture);
		if (UTexture2D* Tex = RPGAssets::Load<UTexture2D>(RPGAssets::ObjPath(TEXT("/Game/RPG/Pixel"), Texture)))
		{
			Keep.Add(TStrongObjectPtr<UTexture2D>(Tex));
			for (int32 F = 0; F < Frames; ++F)
			{
				FSlateBrush& B = Set->AddDefaulted_GetRef();
				B.SetResourceObject(Tex);
				B.DrawAs = ESlateBrushDrawType::Image;
				B.SetUVRegion(FBox2f(FVector2f(float(F) / Frames, 0.f), FVector2f(float(F + 1) / Frames, 1.f)));
			}
		}
	}
	return Set->IsValidIndex(Frame) ? &(*Set)[Frame] : nullptr;
}

int32 SRPGBackdrop::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (Scene.IsEmpty()) return Layer;
	const FVector2D Size = G.GetLocalSize();
	// Anchor: the hero's feet (its frame leaves ~2.5 of 32 px under the boots). Backdrop px -> local px: P = Feet + (bd - (120, 160)) * S.
	FVector2D Feet(Size.X * 0.5f, Size.Y * 0.7f);
	if (TSharedPtr<SWidget> H = Hero.Pin())
	{
		const FGeometry& HG = H->GetPaintSpaceGeometry();   // (not GetCachedGeometry: that is desktop space)
		if (HG.GetLocalSize().X > 1.f) Feet = G.AbsoluteToLocal(HG.LocalToAbsolute(FVector2D(HG.GetLocalSize().X * 0.5f, HG.GetLocalSize().Y * (29.5f / 32.f))));
	}
	const float S = FMath::Max(Size.X / 200.f, Size.Y / 260.f);   // the whole 200 px of height, ~200 of 240 px across
	auto ToLocal = [&](float X, float Y) { return Feet + FVector2D(X - 120.f, Y - 160.f) * S; };
	const float t = FMath::Fmod(T, BDLoop);
	int32 L = Layer;

	auto Rect = [&](const FVector2D& At, const FVector2D& Sz, const FLinearColor& Col)
	{
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Sz, FSlateLayoutTransform(At)), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Col);
	};
	// Draw a texture centred at backdrop (X, Y), W x H backdrop px, rotated Deg about its centre (or its top when bTop).
	auto Draw = [&](const FSlateBrush* B, float X, float Y, float W, float H, const FLinearColor& Tint, float Deg = 0.f, bool bTop = false)
	{
		if (!B || Tint.A <= 0.003f) return;
		const FVector2D Sz(W * S, H * S);
		const FVector2D TopLeft = ToLocal(X, Y) - FVector2D(Sz.X * 0.5f, bTop ? 0.f : Sz.Y * 0.5f);
		if (Deg == 0.f) FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(Sz, FSlateLayoutTransform(TopLeft)), B, ESlateDrawEffect::None, Tint);
		else FSlateDrawElement::MakeRotatedBox(Out, L, G.ToPaintGeometry(Sz, FSlateLayoutTransform(TopLeft)), B, ESlateDrawEffect::None,
			FMath::DegreesToRadians(Deg), FVector2D(Sz.X * 0.5f, bTop ? 0.f : Sz.Y * 0.5f), FSlateDrawElement::RelativeToElement, Tint);
	};

	// Backdrop image (with its floor / sky colour carried past its edges).
	static const TMap<FString, TPair<FLinearColor, FLinearColor>> Edges = {
		{ TEXT("knight"), { FLinearColor(0.02f, 0.05f, 0.2f), FLinearColor(0.05f, 0.15f, 0.04f) } },
		{ TEXT("thief"), { FLinearColor(0.002f, 0.002f, 0.006f), FLinearColor(0.004f, 0.005f, 0.01f) } },
		{ TEXT("mage"), { FLinearColor(0.006f, 0.003f, 0.02f), FLinearColor(0.006f, 0.004f, 0.014f) } },
		{ TEXT("scholar"), { FLinearColor(0.04f, 0.017f, 0.009f), FLinearColor(0.06f, 0.024f, 0.008f) } } };
	const TPair<FLinearColor, FLinearColor>* Edge = Edges.Find(Scene);
	if (Edge) { Rect(FVector2D::ZeroVector, Size, Edge->Key); Rect(FVector2D(0, Feet.Y), FVector2D(Size.X, Size.Y - Feet.Y), Edge->Value); }
	++L;
	Draw(Brush(TEXT("BD_") + Scene), 120.f, 100.f, 240.f, 200.f, FLinearColor::White);
	++L;

	if (Scene == TEXT("knight"))
	{
		// Light rays from the dawn sky, then petals, then gold glints about the castle.
		for (int32 I = 0; I < 5; ++I)
		{
			const float A = 0.16f * Ramp(t, 0.5f, 3.f) * (0.75f + 0.25f * FMath::Sin(T * 0.7f + I));
			Draw(Brush(TEXT("FX_Ray")), 30.f + I * 12.f, -10.f, 26.f, 190.f, FLinearColor(1.f, 0.92f, 0.7f, A), -38.f + I * 7.f + 2.f * FMath::Sin(T * 0.3f + I), true);
		}
		for (int32 I = 0; I < 46; ++I)
		{
			const float Start = 2.f + I * 0.12f;
			if (t < Start) continue;
			const float Speed = 0.05f + Hash(I, 1) * 0.05f;
			const float Ph = FMath::Frac((T - Start) * Speed + Hash(I, 2));
			const float X = 40.f + Hash(I, 3) * 180.f + FMath::Sin(Ph * 12.f + I) * 7.f + Ph * 20.f;
			const float Y = -10.f + Ph * 190.f;
			const FLinearColor Col = Hash(I, 4) < 0.6f ? FLinearColor(1.f, 0.62f, 0.78f) : FLinearColor(1.f, 0.9f, 0.94f);
			Draw(Brush(TEXT("FX_Petal")), X, Y, 5.f, 4.f, FLinearColor(Col.R, Col.G, Col.B, Ramp(t, Start, Start + 1.f) * 0.95f), T * (60.f + Hash(I, 5) * 120.f) + I * 40.f);
		}
		for (int32 I = 0; I < 10; ++I)
		{
			const float A = Ramp(t, 6.f + I * 0.4f, 7.f + I * 0.4f) * FMath::Max(0.f, FMath::Sin(T * 3.f + I * 1.7f));
			Draw(Brush(TEXT("FX_Spark")), 112.f + Hash(I, 6) * 76.f, 10.f + Hash(I, 7) * 120.f, 4.f, 4.f, FLinearColor(1.f, 0.85f, 0.35f, A));
		}
	}
	else if (Scene == TEXT("thief"))
	{
		// Ninjas race across the rooftops (leaping the gaps), then the chest bursts open.
		for (int32 N = 0; N < 3; ++N)
		{
			const float Start = 1.f + N * 2.2f, Run = t - Start;
			if (Run < 0.f) continue;
			const float X = 30.f + Run * 38.f;
			if (X > 230.f) continue;
			const float Hop = FMath::Max(0.f, FMath::Sin((X - 30.f) / 45.f * PI));
			const int32 Frame = Hop > 0.35f ? 4 : int32(T * 12.f) % 4;
			Draw(Brush(TEXT("SPR_Ninja"), 5, Frame), X, 112.f - 7.f - Hop * 12.f, 14.f, 14.f, FLinearColor(0.75f, 0.78f, 0.95f));
		}
		const float Open = 8.f;
		const float Shake = t > Open - 1.f && t < Open ? FMath::Sin(T * 60.f) * 0.8f : 0.f;
		const int32 ChestFrame = t < Open ? 0 : t < Open + 0.25f ? 1 : 2;
		Draw(Brush(TEXT("SPR_Chest"), 3, ChestFrame), 152.f + Shake, 160.f - 12.f, 32.f, 24.f, FLinearColor::White);
		if (t > Open + 0.2f)
		{
			Draw(Brush(TEXT("FX_Orb")), 152.f, 146.f, 40.f, 26.f, FLinearColor(1.f, 0.85f, 0.4f, 0.35f * (1.f - Ramp(t, 12.5f, 14.f)) * (0.8f + 0.2f * FMath::Sin(T * 5.f))));
			for (int32 I = 0; I < 14; ++I)
			{
				const float Tc = t - Open - 0.25f - I * 0.06f;
				if (Tc < 0.f) continue;
				const float Vx = (Hash(I, 8) - 0.5f) * 70.f, Vy = -70.f - Hash(I, 9) * 50.f;
				float Y = 140.f + Vy * Tc + 0.5f * 220.f * Tc * Tc, X = 152.f + Vx * FMath::Min(Tc, 1.2f);
				if (Y > 158.f) Y = 158.f;
				Draw(Brush(TEXT("FX_Coin")), X, Y, 5.f, 5.f, FLinearColor(1, 1, 1, 1.f - Ramp(t, 12.5f, 14.f)), Y < 158.f ? T * 400.f : 0.f);
			}
		}
	}
	else if (Scene == TEXT("mage"))
	{
		// Lightning strikes, then a rune circle kindles behind the hero, sparks rise and orbs circle.
		static const float Strikes[] = { 0.6f, 1.0f, 3.3f, 6.4f, 10.2f };
		float Flash = 0.f;
		for (int32 I = 0; I < 5; ++I)
		{
			const float D = t - Strikes[I];
			if (D < 0.f || D > 0.45f) continue;
			const float A = 1.f - D / 0.45f;
			Flash = FMath::Max(Flash, A);
			Draw(Brush(TEXT("FX_Bolt")), 50.f + Hash(I, 10) * 150.f, -5.f, 30.f, 120.f, FLinearColor(0.9f, 0.85f, 1.f, A), 0.f, true);
		}
		const float Rune = Ramp(t, 4.f, 6.f) * (1.f - Ramp(t, 13.f, 14.f));
		// A summoning circle flat on the ground under the hero (squashed to the floor's perspective; it turns
		// through the frames of FX_RunesSpin), over a soft glow.
		const float Pulse = 0.8f + 0.2f * FMath::Sin(T * 3.f);
		Draw(Brush(TEXT("FX_Orb")), 120.f, 159.f, 96.f, 26.f, FLinearColor(0.65f, 0.45f, 1.f, 0.35f * Rune * Pulse));
		const int32 Spin = int32(T * 6.f) % 12;
		Draw(Brush(TEXT("FX_RunesSpin"), 12, Spin), 120.f, 159.f, 84.f, 24.f, FLinearColor(0.75f, 0.55f, 1.f, 0.9f * Rune * Pulse));
		Draw(Brush(TEXT("FX_RunesSpin"), 12, 11 - Spin), 120.f, 159.f, 52.f, 15.f, FLinearColor(0.55f, 0.88f, 1.f, 0.75f * Rune));
		for (int32 I = 0; I < 30; ++I)
		{
			if (t < 5.f + I * 0.1f) continue;
			const float Ph = FMath::Frac(T * (0.2f + Hash(I, 11) * 0.2f) + Hash(I, 12));
			const float Ang = Hash(I, 13) * 2.f * PI;   // rising from the ring
			Draw(Brush(TEXT("FX_Spark")), 120.f + FMath::Cos(Ang) * 40.f + FMath::Sin(Ph * 9.f + I) * 3.f, 159.f + FMath::Sin(Ang) * 11.f - Ph * 110.f, 3.f, 3.f,
				FLinearColor(0.6f + 0.4f * Hash(I, 14), 0.6f, 1.f, (1.f - Ph) * Rune));
		}
		for (int32 I = 0; I < 3; ++I)
		{
			const float A = T * 1.2f + I * 2.094f;
			Draw(Brush(TEXT("FX_Orb")), 120.f + FMath::Cos(A) * 34.f, 140.f + FMath::Sin(A) * 9.f, 6.f, 6.f, FLinearColor(0.6f, 0.8f, 1.f, Ramp(t, 7.f, 8.f) * (1.f - Ramp(t, 13.f, 14.f))));
		}
		if (Flash > 0.f) Rect(FVector2D::ZeroVector, Size, FLinearColor(0.85f, 0.8f, 1.f, 0.45f * Flash));
	}
	else if (Scene == TEXT("scholar"))
	{
		// A sunbeam with dust, then the book opens on the lectern and its pages flutter out.
		Draw(Brush(TEXT("FX_Ray")), 36.f, 20.f, 40.f, 200.f, FLinearColor(1.f, 0.95f, 0.8f, 0.22f), -32.f, true);
		for (int32 I = 0; I < 26; ++I)
		{
			const float X = 50.f + Hash(I, 15) * 90.f + FMath::Sin(T * 0.4f + I) * 5.f, Y = 40.f + Hash(I, 16) * 110.f + FMath::Cos(T * 0.3f + I * 2.f) * 4.f;
			Draw(Brush(TEXT("FX_Orb")), X, Y, 1.6f, 1.6f, FLinearColor(1.f, 0.95f, 0.8f, 0.5f + 0.5f * FMath::Sin(T * 2.f + I)));
		}
		const int32 BookFrame = t < 2.f ? 0 : t < 2.4f ? 1 : t < 3.2f ? 2 : 3;
		Draw(Brush(TEXT("SPR_Book"), 4, BookFrame), 164.f, 126.f - 9.f, 30.f, 19.f, FLinearColor::White);
		for (int32 I = 0; I < 14; ++I)
		{
			const float U = (t - 3.5f - I * 0.55f) / 4.f;
			if (U < 0.f || U > 1.f) continue;
			const float X = 164.f - U * (40.f + Hash(I, 17) * 60.f) + FMath::Sin(U * 14.f + I) * 6.f;
			const float Y = 112.f - U * (60.f + Hash(I, 18) * 50.f) + FMath::Sin(U * 6.f) * 8.f;
			Draw(Brush(TEXT("FX_Page")), X, Y, 7.f, 9.f, FLinearColor(1, 1, 1, FMath::Min(1.f, (1.f - U) * 3.f)), FMath::Sin(U * 10.f + I) * 50.f);
		}
	}
	++L;

	// Shade the top (title) and bottom (captions) so the text stays readable.
	for (int32 I = 0; I < 10; ++I)
	{
		const float H = Size.Y * 0.03f;
		Rect(FVector2D(0, I * H), FVector2D(Size.X, H), FLinearColor(0, 0, 0, 0.45f * (1.f - I / 10.f)));
		Rect(FVector2D(0, Size.Y - (I + 1) * H), FVector2D(Size.X, H), FLinearColor(0, 0, 0, 0.6f * (1.f - I / 10.f)));
	}
	return L + 1;
}

namespace
{
	constexpr int32 SheetCols = 4, SheetRows = 13;
	constexpr float HeroScale = 10.f;   // screen px per sprite px on the stage
}

FString SRPGCharSelect::Sheet(const FString& Class) const
{
	return TEXT("SPR_") + Class + (Sex == TEXT("female") ? TEXT("_f") : TEXT("_m"));
}

void SRPGCharSelect::SetBrush(FSlateBrush& B, const FString& Texture, const FVector2D& Size)
{
	UTexture2D* Tex = RPGAssets::Load<UTexture2D>(RPGAssets::ObjPath(TEXT("/Game/RPG/Pixel"), Texture));
	if (Tex) Keep.Add(TStrongObjectPtr<UTexture2D>(Tex));
	B = FSlateBrush();
	B.SetResourceObject(Tex);
	B.ImageSize = Size;
	B.DrawAs = ESlateBrushDrawType::Image;
}

void SRPGCharSelect::SetFrame(FSlateBrush& B, int32 Row, int32 Col)
{
	B.SetUVRegion(FBox2f(FVector2f(float(Col) / SheetCols, float(Row) / SheetRows), FVector2f(float(Col + 1) / SheetCols, float(Row + 1) / SheetRows)));
}

void SRPGCharSelect::Construct(const FArguments& Args)
{
	World = Args._World;
	OnBegin = Args._OnBegin;
	OnPreview = Args._OnPreview;
	OnBack = Args._OnBack;
	const RPGJson::FObj Classes = URPGData::Get(World.Get()).Section(TEXT("classes"));
	for (const auto& KV : Classes->Values) ClassIds.Add(FString(*KV.Key));

	SetBrush(StageBrush, TEXT("UI_Stage"), FVector2D(96, 24) * 5.f);
	SetBrush(ShadowBrush, TEXT("PR_Shadow"), FVector2D(28, 10) * 8.f);
	SetBrush(FxBrush, TEXT("FX_Orb"), FVector2D(12, 12) * 4.f);
	for (const TCHAR* I : { TEXT("sword"), TEXT("dagger"), TEXT("shield"), TEXT("buckler"), TEXT("bow"), TEXT("staff"), TEXT("speech") })
		SetBrush(Icons.Add(I), FString(TEXT("ICO_")) + I, FVector2D(32, 32));

	const FLinearColor BgColor(0.012f, 0.011f, 0.018f, 1.f);   // (Slate colours are linear: this is a near-black)
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SImage).Image(White()).ColorAndOpacity(BgColor) ]
		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox)
			// ---- left: the hero on stage
			+ SHorizontalBox::Slot().FillWidth(0.44f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SAssignNew(Backdrop, SRPGBackdrop) ]
				+ SOverlay::Slot().Padding(40, 30, 10, 30)
				[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Font(Font(34, TEXT("Bold")))
					.Text_Lambda([this]() { return T(RPGJson::Str(URPGData::Get(World.Get()).Entry(TEXT("classes"), ClassId), TEXT("name"))); })
					.ColorAndOpacity_Lambda([this]() { return FSlateColor(RPGJson::Color(RPGJson::Str(URPGData::Get(World.Get()).Entry(TEXT("classes"), ClassId), TEXT("color")))); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
				[
					SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Muted).AutoWrapText(true)
					.Text_Lambda([this]() { return T(RPGJson::Str(URPGData::Get(World.Get()).Entry(TEXT("classes"), ClassId), TEXT("tagline"))); })
				]
				+ SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center).HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(560).HeightOverride(470)
					[
						SNew(SOverlay)
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom)[ SAssignNew(StageImage, SImage).Image(&StageBrush).Visibility(EVisibility::Collapsed) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 92)[ SNew(SImage).Image(&ShadowBrush).ColorAndOpacity(FLinearColor(1, 1, 1, 0.35f)) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 70)
						[
							SNew(SBox).WidthOverride(32 * HeroScale).HeightOverride(32 * HeroScale)
							[
								SNew(SOverlay)
								+ SOverlay::Slot()[ SAssignNew(HeroImage, SImage).Image(&HeroBrush).RenderTransformPivot(FVector2D(0.5f, 0.5f)) ]
								+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SAssignNew(FxImage, SImage).Image(&FxBrush).RenderTransformPivot(FVector2D(0.5f, 0.5f)) ]
								+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
								[ SAssignNew(PopText, STextBlock).Font(Font(22, TEXT("Bold"))).ColorAndOpacity(Gold).ShadowOffset(FVector2D(2, 2)).Text(T(TEXT("BLOCK!"))) ]
							]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 14, 0, 0)
				[
					SNew(STextBlock).Font(Font(18, TEXT("Bold"))).ColorAndOpacity(Gold)
					.Text_Lambda([this]() { return Steps.IsValidIndex(StepIdx) ? T(Steps[StepIdx].Caption) : FText::GetEmpty(); })
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)
				[
					SNew(SBox).WidthOverride(560)
					[
						SNew(STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(0.86f, 0.86f, 0.84f)).AutoWrapText(true).Justification(ETextJustify::Center)
						.Text_Lambda([this]() { return Steps.IsValidIndex(StepIdx) ? T(Steps[StepIdx].Detail) : FText::GetEmpty(); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)
				[
					SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted)
					.Text_Lambda([this]() { return T(FString::Printf(TEXT("showing %d of %d"), StepIdx + 1, Steps.Num())); })
				]
				]
			]
			// ---- right: the choices
			+ SHorizontalBox::Slot().FillWidth(0.56f).Padding(10, 26, 36, 26)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(PanelColor).Padding(FMargin(24, 18))
				[ SAssignNew(Right, SBox) ]
			]
		]
	];
	Backdrop->SetHero(HeroImage);   // (built in the same tree, after the backdrop)
	Select(ClassId);
}

void SRPGCharSelect::Select(const FString& InClass)
{
	ClassId = InClass;
	SetBrush(HeroBrush, Sheet(ClassId), FVector2D(32, 32) * HeroScale);
	for (int32 I = 0; I < 4 && I < ClassIds.Num(); ++I) SetBrush(CardBrushes[I], Sheet(ClassIds[I]), FVector2D(64, 64));
	BuildShowcase();
	Rebuild();
	if (Backdrop) Backdrop->SetScene(ClassId);
	OnPreview.ExecuteIfBound(ClassId, Sex);
}

void SRPGCharSelect::SetSex(const FString& InSex)
{
	Sex = InSex;
	Select(ClassId);
}

void SRPGCharSelect::BuildShowcase()
{
	const URPGData& D = URPGData::Get(World.Get());
	const RPGJson::FObj C = D.Entry(TEXT("classes"), ClassId);
	const TArray<TSharedPtr<FJsonValue>> Styles = RPGJson::Arr(C, TEXT("styles"));
	const RPGJson::FObj St = Styles.Num() ? D.Entry(TEXT("weaponStyles"), Styles[0]->AsString()) : nullptr;
	const RPGJson::FObj Primary = RPGJson::Obj(St, TEXT("primary")), Secondary = RPGJson::Obj(St, TEXT("secondary")), Passive = RPGJson::Obj(St, TEXT("passive"));
	auto Step = [this](const FString& Cap, const FString& Det, int32 Dir, int32 Act, float Time, FName Fx = NAME_None, FLinearColor Col = FLinearColor::White, bool bFlip = false, int32 Ab = -1)
	{
		FStep S; S.Caption = Cap; S.Detail = Det; S.Dir = Dir; S.Act = Act; S.Time = Time; S.Fx = Fx; S.FxColor = Col; S.bFlip = bFlip; S.Ability = Ab;
		Steps.Add(S);
	};
	Steps.Reset();
	StepIdx = 0;
	StepT = 0.f;

	Step(TEXT("Idle"), TEXT("Ready for anything."), 0, 0, 1.6f);
	const FString Walk = TEXT("Click the ground to walk there (WASD works too).");
	Step(TEXT("Walk"), Walk, 0, 1, 0.9f);
	Step(TEXT("Walk"), Walk, 2, 1, 0.9f);
	Step(TEXT("Walk"), Walk, 1, 1, 0.9f);
	Step(TEXT("Walk"), Walk, 2, 1, 0.9f, NAME_None, FLinearColor::White, true);

	// Primary attack (left click).
	const FString StyleName = RPGJson::Str(St, TEXT("name"));
	if (RPGJson::Str(Primary, TEXT("type")) == TEXT("bolt"))
		Step(StyleName + TEXT(": Arcane Bolt (left click)"), TEXT("A free magic bolt from the staff, fired as fast as it recharges."), 2, 2, 2.7f, TEXT("orb"), RPGJson::Color(RPGJson::Str(Primary, TEXT("color"), TEXT("#b9a4ff"))));
	else
	{
		const FString Det = ClassId == TEXT("thief") ? TEXT("A fast 4-hit dagger combo. Hit a foe from behind for double damage.")
			: FString::Printf(TEXT("A %d-hit combo. Click and hold to keep swinging."), RPGJson::Arr(Primary, TEXT("combo")).Num());
		Step(StyleName + TEXT(": combo (left click)"), Det, 0, 2, 1.8f);
		Step(StyleName + TEXT(": combo (left click)"), Det, 2, 2, 1.8f);
	}

	// Defence: shield block, bow, or mana shield.
	const FString SecType = RPGJson::Str(Secondary, TEXT("type"));
	if (SecType == TEXT("block") && RPGJson::Bool(Secondary, TEXT("barrier")))
		Step(TEXT("Arcane barrier (hold right click)"), TEXT("A shield of light all around you: 80% less damage from every side, but it drains stamina while you hold it."),
			0, 0, 2.6f, TEXT("bubble"), FLinearColor(0.55f, 0.48f, 1.f));
	else if (SecType == TEXT("block"))
	{
		const bool bPerfect = RPGJson::Num(Secondary, TEXT("perfectWindow"), 0) > 0;
		const FString Det = FString::Printf(TEXT("Hold right click to block %d%% of a frontal hit; it drains stamina while held.%s"), FMath::RoundToInt(RPGJson::Num(Secondary, TEXT("reduction"), 0.5) * 100.0),
			bPerfect ? TEXT(" Raise it just before the blow for a perfect block.") : TEXT(""));
		Step(TEXT("Shield block (right click)"), Det, 0, 4, 1.3f, TEXT("block"));
		Step(TEXT("Shield block (right click)"), Det, 2, 4, 1.3f, TEXT("block"));
	}
	else if (SecType == TEXT("bow"))
		Step(TEXT("Bow (hold right click, release)"), TEXT("Unlimited arrows that arc onto the target. A full draw hits harder and flies further."), 2, 0, 2.7f, TEXT("arrow"), FLinearColor(1.f, 0.85f, 0.3f));


	// Abilities.
	const TArray<TSharedPtr<FJsonValue>> Abilities = RPGJson::Arr(C, TEXT("abilities"));
	for (int32 I = 0; I < Abilities.Num(); ++I)
	{
		const RPGJson::FObj A = D.Entry(TEXT("abilities"), Abilities[I]->AsString());
		const FString Type = RPGJson::Str(A, TEXT("type"));
		const FLinearColor Col = RPGJson::Color(RPGJson::Str(A, TEXT("color"), TEXT("#ffffff")));
		const FString Cap = FString::Printf(TEXT("%s  (key %d, level %d)"), *RPGJson::Str(A, TEXT("name")), I + 1, int32(RPGJson::Num(A, TEXT("unlockLevel"), 1)));
		const FString Det = RPGJson::Str(A, TEXT("desc"));
		if (Type == TEXT("projectile")) Step(Cap, Det, 2, 2, 2.4f, RPGJson::Bool(A, TEXT("arrow")) ? FName(TEXT("arrow")) : FName(TEXT("orb")), Col, false, I);
		else if (Type == TEXT("heal")) Step(Cap, Det, 0, 0, 2.4f, TEXT("heal"), Col, false, I);
		else if (Type == TEXT("buff")) Step(Cap, Det, 0, 0, 2.4f, TEXT("ring"), Col, false, I);
		else if (Type == TEXT("dashStrike") || Type == TEXT("blink")) Step(Cap, Det, 2, 1, 2.4f, TEXT("ring"), Col, false, I);
		else Step(Cap, Det, Type == TEXT("aoe") || Type == TEXT("smoke") ? 0 : 2, 2, 2.4f, TEXT("ring"), Col, false, I);
	}
}

void SRPGCharSelect::Rebuild()
{
	const URPGData& D = URPGData::Get(World.Get());
	const RPGJson::FObj C = D.Entry(TEXT("classes"), ClassId);
	const FLinearColor Accent = RPGJson::Color(RPGJson::Str(C, TEXT("color")));

	// Class cards with a live portrait.
	auto Cards = SNew(SHorizontalBox);
	for (int32 I = 0; I < ClassIds.Num(); ++I)
	{
		const FString Id = ClassIds[I];
		const RPGJson::FObj K = D.Entry(TEXT("classes"), Id);
		const bool bSel = Id == ClassId;
		Cards->AddSlot().FillWidth(1).Padding(4)
		[
			SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(bSel ? FRPGChoose::Highlighted() : FRPGChoose::Idle())
			.OnClicked_Lambda([this, Id]() { Select(Id); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SImage).Image(I < 4 ? &CardBrushes[I] : nullptr) ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(4, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(RPGJson::Color(RPGJson::Str(K, TEXT("color")))).Text(T(RPGJson::Str(K, TEXT("name")))) ]
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted).Text(T(FString::Printf(TEXT("key %d"), I + 1))) ]
				]
			]
		];
	}

	// Sex.
	auto SexButton = [this](const FString& Value, const FString& Label)
	{
		return SNew(SButton).IsFocusable(false).HAlign(HAlign_Center).ButtonColorAndOpacity(Sex == Value ? FRPGChoose::Highlighted() : FRPGChoose::Idle())
			.OnClicked_Lambda([this, Value]() { SetSex(Value); return FReply::Handled(); })[ SNew(STextBlock).Font(Font(12)).Text(T(Label)) ];
	};

	// Stats: attributes and the key derived numbers, as compact bars.
	auto Stats = SNew(SVerticalBox);
	auto Derived = SNew(SVerticalBox);
	auto Row = [&](const TSharedRef<SVerticalBox>& Into, const FString& Label, float Value, float Max, const FLinearColor& Col, const FString& Shown)
	{
		Into->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(92)[ SNew(STextBlock).Font(Font(11)).Text(T(Label)) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Bar(150, 8, Col, [Value, Max]() { return FMath::Max(0.03f, Value / FMath::Max(Max, 0.01f)); }) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[ SNew(STextBlock).Font(Font(11, TEXT("Bold"))).Text(T(Shown)) ]
		];
	};
	const RPGJson::FObj Attr = RPGJson::Obj(C, TEXT("attributes"));
	for (const TCHAR* K : { TEXT("might"), TEXT("agility"), TEXT("focus"), TEXT("vitality"), TEXT("presence") })
	{
		FString Label = K; Label[0] = FChar::ToUpper(Label[0]);
		Row(Stats, Label, float(RPGJson::Num(Attr, K)), 10.f, Accent, FString::FromInt(int32(RPGJson::Num(Attr, K))));
	}
	TArray<FClassPreview> All;
	for (const FString& Id : ClassIds) All.Add(Preview(D, Id));
	const FClassPreview P = Preview(D, ClassId);
	auto Max = [&](float FClassPreview::* M) { float V = 0; for (const FClassPreview& X : All) V = FMath::Max(V, X.*M); return V; };
	Row(Derived, TEXT("Health"), P.HP, Max(&FClassPreview::HP), FLinearColor(0.84f, 0.27f, 0.27f), FString::Printf(TEXT("%.0f"), P.HP));
	Row(Derived, TEXT("Stamina"), P.Stamina, Max(&FClassPreview::Stamina), FLinearColor(0.5f, 0.82f, 0.5f), FString::Printf(TEXT("%.0f"), P.Stamina));
	Row(Derived, TEXT("Mana"), P.Mana, Max(&FClassPreview::Mana), FLinearColor(0.31f, 0.5f, 0.88f), FString::Printf(TEXT("%.0f"), P.Mana));
	Row(Derived, TEXT("Speed"), P.Speed, Max(&FClassPreview::Speed), FLinearColor(0.9f, 0.9f, 0.9f), FString::Printf(TEXT("%.0f"), P.Speed));
	Row(Derived, TEXT("Armor"), P.Armor, FMath::Max(1.f, Max(&FClassPreview::Armor)), Muted, FString::Printf(TEXT("%.0f"), P.Armor));

	// Kit: weapons and defence with icons, dialogue style.
	const TArray<TSharedPtr<FJsonValue>> StyleIds = RPGJson::Arr(C, TEXT("styles"));
	const RPGJson::FObj St = StyleIds.Num() ? D.Entry(TEXT("weaponStyles"), StyleIds[0]->AsString()) : nullptr;
	TArray<FString> KitIcons;
	const FString Kit = StyleIds.Num() ? StyleIds[0]->AsString() : FString();
	if (Kit.Contains(TEXT("sword")) || Kit == TEXT("longsword")) KitIcons.Add(TEXT("sword"));
	if (Kit.Contains(TEXT("dagger"))) KitIcons.Add(TEXT("dagger"));
	if (Kit == TEXT("staff")) KitIcons.Add(TEXT("staff"));
	if (Kit.Contains(TEXT("bow"))) KitIcons.Add(TEXT("bow"));
	if (Kit == TEXT("sword_shield")) KitIcons.Add(TEXT("shield"));
	if (Kit == TEXT("dagger_shield")) KitIcons.Add(TEXT("buckler"));
	auto IconRow = SNew(SHorizontalBox);
	for (const FString& I : KitIcons) IconRow->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[ SNew(SImage).Image(Icons.Find(I)) ];
	auto Info = SNew(SVerticalBox);
	auto Section = [&](const FString& Head, const FString& Text, const FLinearColor& HeadCol)
	{
		Info->AddSlot().AutoHeight().Padding(0, 6, 0, 1)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(HeadCol).Text(T(Head)) ];
		Info->AddSlot().AutoHeight()[ SNew(STextBlock).Font(Font(11)).AutoWrapText(true).Text(T(Text)) ];
	};
	Info->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[ IconRow ];
	Section(TEXT("WEAPONS"), RPGJson::Str(C, TEXT("weapons")), Muted);
	Section(TEXT("DEFENSE"), RPGJson::Str(C, TEXT("defense")), Muted);
	const RPGJson::FObj Dlg = RPGJson::Obj(C, TEXT("dialogue"));
	Section(TEXT("TALKS WITH: ") + RPGJson::Str(Dlg, TEXT("style")).ToUpper(), RPGJson::Str(Dlg, TEXT("desc")), Accent);

	// Abilities: the one being demonstrated on the left lights up.
	auto Abil = SNew(SVerticalBox);
	const TArray<TSharedPtr<FJsonValue>> Ids = RPGJson::Arr(C, TEXT("abilities"));
	for (int32 I = 0; I < Ids.Num(); ++I)
	{
		const RPGJson::FObj A = D.Entry(TEXT("abilities"), Ids[I]->AsString());
		const FLinearColor Col = RPGJson::Color(RPGJson::Str(A, TEXT("color"), TEXT("#ffffff")));
		Abil->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SBorder).BorderImage(White()).Padding(FMargin(8, 5))
			.BorderBackgroundColor_Lambda([this, I]() { const bool bOn = Steps.IsValidIndex(StepIdx) && Steps[StepIdx].Ability == I;
				return FSlateColor(bOn ? FLinearColor(0.32f, 0.28f, 0.14f) : FLinearColor(0.1f, 0.11f, 0.14f)); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[ SNew(SBox).WidthOverride(14).HeightOverride(14)[ SNew(SImage).Image(White()).ColorAndOpacity(Col) ] ]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[ SNew(STextBlock).Font(Font(12, TEXT("Bold"))).Text(T(FString::Printf(TEXT("%d  %s"), I + 1, *RPGJson::Str(A, TEXT("name"))))) ]
					+ SVerticalBox::Slot().AutoHeight()
					[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.78f)).AutoWrapText(true).Text(T(RPGJson::Str(A, TEXT("desc")))) ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted).Text(T(FString::Printf(TEXT("Lv %d"), int32(RPGJson::Num(A, TEXT("unlockLevel"), 1))))) ]
			]
		];
	}

	Right->SetContent(
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(24, TEXT("Bold"))).Text(LOCTEXT("Choose", "Choose your hero")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).Text(LOCTEXT("ChooseHint2", "Click a class (or 1-4), pick M / F, then Begin (Enter). Esc to go back.")) ]
			+ SVerticalBox::Slot().AutoHeight()[ Cards ]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 6, 4, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)[ SexButton(TEXT("male"), TEXT("Male (M)")) ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)[ SexButton(TEXT("female"), TEXT("Female (F)")) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 3)
			[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted).AutoWrapText(true).Text(LOCTEXT("SexNote2", "Stats are identical; some people react differently to you.")) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 22, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Muted).Text(LOCTEXT("Attrs2", "ATTRIBUTES")) ]
					+ SVerticalBox::Slot().AutoHeight()[ Stats ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Muted).Text(LOCTEXT("AtLv1", "AT LEVEL 1")) ]
					+ SVerticalBox::Slot().AutoHeight()[ Derived ]
				]
				+ SHorizontalBox::Slot().FillWidth(1)[ Info ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 4)[ SNew(STextBlock).Font(Font(10, TEXT("Bold"))).ColorAndOpacity(Muted).Text(LOCTEXT("Abil2", "ABILITIES (keys 1-4, or Shift + wheel)")) ]
			+ SVerticalBox::Slot().AutoHeight()[ Abil ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).IsFocusable(false).ContentPadding(FMargin(16, 8))
					.ButtonColorAndOpacity_Lambda([this]() { return Fx.Color(0, HoverButton); })
					.OnHovered_Lambda([this]() { HoverButton = 0; }).OnUnhovered_Lambda([this]() { if (HoverButton == 0) HoverButton = -1; })
					.OnClicked_Lambda([this]() { Fx.Start(0, [this]() { OnBack.ExecuteIfBound(); }); return FReply::Handled(); })
					[ SNew(STextBlock).Font(Font(13)).Text(LOCTEXT("Back", "Back")) ]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).IsFocusable(false).ContentPadding(FMargin(22, 8))
					.ButtonColorAndOpacity_Lambda([this]() { return Fx.Item == 1 ? FRPGChoose::Chosen() : HoverButton == 1 ? FRPGChoose::Highlighted() : FLinearColor(0.3f, 0.22f, 0.06f); })
					.OnHovered_Lambda([this]() { HoverButton = 1; }).OnUnhovered_Lambda([this]() { if (HoverButton == 1) HoverButton = -1; })
					.OnClicked_Lambda([this]() { Fx.Start(1, [this]() { OnBegin.ExecuteIfBound(ClassId, Sex); }); return FReply::Handled(); })
					[ SNew(STextBlock).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(Gold)
						.Text(T(FString::Printf(TEXT("Begin as %s %s"), Sex == TEXT("female") ? TEXT("Female") : TEXT("Male"), *RPGJson::Str(C, TEXT("name"))))) ]
				]
			]
		]);
}

void SRPGCharSelect::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SCompoundWidget::Tick(G, Time, Dt);
	Clock += Dt;
	SetRenderOpacity(Fx.Tick(Dt));
	if (!HasKeyboardFocus() && GetVisibility().IsVisible()) FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
	if (Steps.IsEmpty()) return;
	StepT += Dt;
	if (StepT >= Steps[StepIdx].Time) { StepT = 0.f; StepIdx = (StepIdx + 1) % Steps.Num(); }
	const FStep& S = Steps[StepIdx];

	// The hero's frame (same layout and timing as the game: RPGSprite.cpp).
	const float Beat = FMath::Fmod(StepT, 0.9f);   // attack / effect cycle
	int32 Row = S.Dir * 4, Col = 0;
	switch (S.Act)
	{
	case 1: Row += 1; Col = int32(Clock * 9.f) % 4; break;
	case 2: Row += 2; Col = Beat < 0.3f ? 0 : Beat < 0.4f ? 1 : Beat < 0.5f ? 2 : Beat < 0.62f ? 3 : 0; break;
	case 3: Row += 3; break;
	case 4: Row = 12; Col = 1 + S.Dir; break;
	default: Col = int32(Clock * 2.2f) % 2; break;
	}
	SetFrame(HeroBrush, Row, Col);
	HeroImage->SetRenderTransform(FSlateRenderTransform(FScale2D(S.bFlip ? -1.f : 1.f, 1.f)));
	for (int32 I = 0; I < 4; ++I) SetFrame(CardBrushes[I], 0, int32(Clock * 2.2f + I) % 2);

	// The step's effect.
	FxImage->SetVisibility(EVisibility::Collapsed);
	PopText->SetVisibility(EVisibility::Collapsed);
	auto Show = [&](const TCHAR* Tex, const FVector2D& Size, const FVector2D& At, float Scale, float Angle, float Alpha)
	{
		if (FxBrush.GetResourceName() != FName(*(FString(TEXT("FX_")) + Tex))) SetBrush(FxBrush, FString(TEXT("FX_")) + Tex, Size);
		FxImage->SetVisibility(EVisibility::HitTestInvisible);
		FxImage->SetColorAndOpacity(FLinearColor(S.FxColor.R, S.FxColor.G, S.FxColor.B, Alpha));
		FxImage->SetRenderTransform(FSlateRenderTransform(FTransform2D(FScale2D(Scale)).Concatenate(FTransform2D(FQuat2D(FMath::DegreesToRadians(Angle)))).Concatenate(FTransform2D(At))));
	};
	if (S.Fx == TEXT("orb") && Beat >= 0.4f)
	{
		const float t = (Beat - 0.4f) / 0.5f;
		Show(TEXT("Orb"), FVector2D(12, 12) * 4.f, FVector2D(80.f + t * 330.f, -10.f), 1.f, 0.f, 1.f);
	}
	else if (S.Fx == TEXT("arrow") && Beat >= 0.25f)
	{
		const float t = (Beat - 0.25f) / 0.65f;
		const float X = 70.f + t * 380.f, Y = -20.f - FMath::Sin(t * PI) * 120.f + t * 120.f;
		const float Slope = FMath::RadiansToDegrees(FMath::Atan2(-FMath::Cos(t * PI) * 120.f * PI + 120.f, 380.f));
		Show(TEXT("Arrow"), FVector2D(24, 7) * 4.f, FVector2D(X, Y), 1.f, Slope, 1.f);
	}
	else if (S.Fx == TEXT("ring"))
	{
		const float t = Beat / 0.9f;
		Show(TEXT("Ring"), FVector2D(48, 48) * 4.f, FVector2D(0.f, 110.f), 0.3f + t * 1.4f, 0.f, 1.f - t);
	}
	else if (S.Fx == TEXT("bubble"))
		Show(TEXT("Ring"), FVector2D(48, 48) * 4.f, FVector2D(0.f, 10.f), 1.45f + 0.04f * FMath::Sin(Clock * 6.f), 0.f, 0.55f);
	else if (S.Fx == TEXT("heal"))
	{
		const float t = Beat / 0.9f;
		Show(TEXT("Heal"), FVector2D(9, 9) * 5.f, FVector2D(40.f * FMath::Sin(Clock * 3.f), 90.f - t * 220.f), 1.f, 0.f, 1.f - t * t);
	}
	else if (S.Fx == TEXT("block") && Beat >= 0.15f && Beat < 0.65f)
	{
		PopText->SetVisibility(EVisibility::HitTestInvisible);
		PopText->SetRenderTransform(FSlateRenderTransform(FTransform2D(FScale2D(1.f + 0.3f * FMath::Max(0.f, 0.3f - (Beat - 0.15f)) / 0.3f), FVector2D(0.f, -175.f))));
	}
}

FReply SRPGCharSelect::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (Fx.Busy()) return FReply::Handled();
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
	const FKey K = E.GetKey();
	for (int32 I = 0; I < 4 && I < ClassIds.Num(); ++I) if (K == Digits[I]) { Select(ClassIds[I]); return FReply::Handled(); }
	const int32 Cur = ClassIds.IndexOfByKey(ClassId);
	if (K == EKeys::Left || K == EKeys::A || K == EKeys::Gamepad_DPad_Left) { Select(ClassIds[(Cur + ClassIds.Num() - 1) % ClassIds.Num()]); return FReply::Handled(); }
	if (K == EKeys::Right || K == EKeys::D || K == EKeys::Gamepad_DPad_Right) { Select(ClassIds[(Cur + 1) % ClassIds.Num()]); return FReply::Handled(); }
	if (K == EKeys::M) { SetSex(TEXT("male")); return FReply::Handled(); }
	if (K == EKeys::F) { SetSex(TEXT("female")); return FReply::Handled(); }
	if (K == EKeys::Gamepad_FaceButton_Top) { SetSex(Sex == TEXT("male") ? TEXT("female") : TEXT("male")); return FReply::Handled(); }   // Y toggles
	if (K == EKeys::Enter || K == EKeys::Gamepad_FaceButton_Bottom) { Fx.Start(1, [this]() { OnBegin.ExecuteIfBound(ClassId, Sex); }); return FReply::Handled(); }
	if (K == EKeys::Escape || K == EKeys::Gamepad_FaceButton_Right) { Fx.Start(0, [this]() { OnBack.ExecuteIfBound(); }); return FReply::Handled(); }
	return FReply::Unhandled();
}

// =============================================================================================
// Title screen
// =============================================================================================

void SRPGTitle::Construct(const FArguments& Args)
{
	OnStart = Args._OnStart;
	OnQuit = Args._OnQuit;
	if (UTexture2D* Fade = RPGAssets::Load<UTexture2D>(TEXT("/Game/RPG/Pixel/UI_Fade.UI_Fade")))
	{
		FadeTex.Reset(Fade);
		FadeBrush.SetResourceObject(Fade);
		FadeBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	const RPGJson::FObj TitleData = URPGData::Get(Args._World.Get()).Section(TEXT("title"));
	const FString Name = RPGJson::Str(TitleData, TEXT("name"), TEXT("Action RPG"));
	const FString Tagline = RPGJson::Str(TitleData, TEXT("tagline"));

	auto Item = [this](int32 I, const FString& Label)
	{
		return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(18, 10)).HAlign(HAlign_Left)
			.ButtonColorAndOpacity_Lambda([this, I]() { return Fx.Color(I, Highlight); })
			.OnHovered_Lambda([this, I]() { if (!Fx.Busy()) Highlight = I; })
			.OnClicked_Lambda([this, I]() { Activate(I); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Font(20, TEXT("Bold"))).Text(T(Label)).ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.93f))
			];
	};
	ChildSlot
	[
		SNew(SOverlay)
		// A soft dark band down the left so the text reads over the world.
		+ SOverlay::Slot().HAlign(HAlign_Left)[ SNew(SBox).WidthOverride(1100)[ SNew(SImage).Image(&FadeBrush).ColorAndOpacity(FLinearColor(1, 1, 1, 0.85f)) ] ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(90, 0, 0, 40)
		[
			SNew(SBox).WidthOverride(560)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(64, TEXT("Bold"))).ColorAndOpacity(Gold).ShadowOffset(FVector2D(3, 3)).Text(T(Name)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 2, 0, 46)[ SNew(STextBlock).Font(Font(16)).ColorAndOpacity(FLinearColor(0.88f, 0.86f, 0.8f)).ShadowOffset(FVector2D(1, 1)).Text(T(Tagline)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)[ SNew(SBox).WidthOverride(340)[ Item(0, TEXT("Start New Game")) ] ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)[ SNew(SBox).WidthOverride(340)[ Item(1, TEXT("Quit")) ] ]
				+ SVerticalBox::Slot().AutoHeight().Padding(4, 30, 0, 0)
				[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Muted).Text(T(TEXT("\u2191\u2193 or wheel, Enter to choose"))) ]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0, 0, 24, 16)
		[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted).Text(T(TEXT("prototype  \u00b7  placeholder art"))) ]
	];
}

void SRPGTitle::Activate(int32 Index)
{
	Highlight = Index;
	Fx.Start(Index, [this, Index]() { if (Index == 0) OnStart.ExecuteIfBound(); else OnQuit.ExecuteIfBound(); });
}

FReply SRPGTitle::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (Fx.Busy()) return FReply::Handled();
	const FKey K = E.GetKey();
	if (K == EKeys::Up || K == EKeys::W || K == EKeys::Gamepad_DPad_Up || K == EKeys::Down || K == EKeys::S || K == EKeys::Gamepad_DPad_Down) { Highlight = 1 - Highlight; return FReply::Handled(); }
	if (K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::Gamepad_FaceButton_Bottom) { Activate(Highlight); return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SRPGTitle::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	if (!Fx.Busy()) Highlight = 1 - Highlight;
	return FReply::Handled();
}

void SRPGTitle::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SCompoundWidget::Tick(G, Time, Dt);
	Clock += Dt;
	SetRenderOpacity(Fx.Tick(Dt));
	if (!HasKeyboardFocus() && GetVisibility().IsVisible()) FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
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
					SNew(SButton).IsFocusable(false).IsEnabled(P->AttrPoints > 0).OnClicked_Lambda([this, W, Stat]() {
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
				SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor(0.12f, 0.13f, 0.16f)).ToolTipText(T(It ? ItemTip(*It) : FString()))
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
					SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor(0.12f, 0.13f, 0.16f)).ToolTipText(T(ItemTip(It)))
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
	[ SNew(SButton).IsFocusable(false).OnClicked_Lambda([this]() { OnClose.ExecuteIfBound(); return FReply::Handled(); })[ SNew(STextBlock).Font(Font(12)).Text(LOCTEXT("Close", "Close (Esc)")) ] ];
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

// =============================================================================================
// Pause menu
// =============================================================================================

void SRPGPauseMenu::Construct(const FArguments& Args)
{
	OnResume = Args._OnResume;
	OnNewGame = Args._OnNewGame;
	OnQuit = Args._OnQuit;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SImage).Image(White()).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.02f, 0.55f)) ]   // dim the paused world
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(440)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(PanelColor).Padding(FMargin(30, 24))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(Font(26, TEXT("Bold"))).ColorAndOpacity(Gold)
						.Text_Lambda([this]() { return T(Page == EPage::Main ? TEXT("Paused") : Page == EPage::ConfirmNew ? TEXT("Start a new game?") : TEXT("Quit the game?")); })
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 18)
					[
						SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Muted).Justification(ETextJustify::Center).AutoWrapText(true)
						.Text_Lambda([this]() { return T(Page == EPage::Main ? TEXT("The world waits for you.")
							: TEXT("There are no saves yet: your current progress will be lost.")); })
					]
					+ SVerticalBox::Slot().AutoHeight()[ SAssignNew(List, SVerticalBox) ]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 16, 0, 0)
					[
						SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Muted)
						.Text_Lambda([this]() { return T(Page == EPage::Main ? TEXT("Esc to resume   \u00b7   \u2191\u2193 or wheel, Enter to choose")
							: TEXT("Esc to go back   \u00b7   \u2191\u2193 or wheel, Enter to choose")); })
					]
				]
			]
		]
	];
	Rebuild();
}

TArray<FString> SRPGPauseMenu::Items() const
{
	if (Page == EPage::ConfirmNew) return { TEXT("Yes, start over"), TEXT("Cancel") };
	if (Page == EPage::ConfirmQuit) return { TEXT("Yes, quit"), TEXT("Cancel") };
	return { TEXT("Resume"), TEXT("New Game"), TEXT("Quit Game") };
}

void SRPGPauseMenu::Rebuild()
{
	List->ClearChildren();
	const TArray<FString> Names = Items();
	for (int32 I = 0; I < Names.Num(); ++I)
	{
		List->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SButton).IsFocusable(false).HAlign(HAlign_Center).ContentPadding(FMargin(10, 9))
			.ButtonColorAndOpacity_Lambda([this, I]() { return Fx.Color(I, Highlight); })
			.OnHovered_Lambda([this, I]() { if (!Fx.Busy()) Highlight = I; })
			.OnClicked_Lambda([this, I]() { Choose(I); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(Font(16, TEXT("Bold"))).Text(T(Names[I])).ColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.93f))
			]
		];
	}
}

void SRPGPauseMenu::Open()
{
	Fx.Reset();
	Page = EPage::Main;
	Highlight = 0;
	Rebuild();
}

void SRPGPauseMenu::Back()
{
	if (Page == EPage::Main) { OnResume.ExecuteIfBound(); return; }
	Highlight = Page == EPage::ConfirmNew ? 1 : 2;   // back onto the item you came from
	Page = EPage::Main;
	Rebuild();
}

void SRPGPauseMenu::Activate(int32 Index)
{
	if (Page == EPage::Main)
	{
		if (Index == 0) { OnResume.ExecuteIfBound(); return; }
		Page = Index == 1 ? EPage::ConfirmNew : EPage::ConfirmQuit;
		Highlight = 1;   // default to Cancel: a slip of Enter shouldn't throw progress away
		Rebuild();
		return;
	}
	if (Index != 0) { Back(); return; }
	if (Page == EPage::ConfirmNew) OnNewGame.ExecuteIfBound();
	else OnQuit.ExecuteIfBound();
}

FReply SRPGPauseMenu::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
{
	if (Fx.Busy()) return FReply::Handled();
	const FKey K = E.GetKey();
	const int32 N = Items().Num();
	if (K == EKeys::Escape || K == EKeys::Gamepad_FaceButton_Right || K == EKeys::Gamepad_Special_Right) { Back(); return FReply::Handled(); }
	if (K == EKeys::Up || K == EKeys::W || K == EKeys::Gamepad_DPad_Up) { Highlight = (Highlight + N - 1) % N; return FReply::Handled(); }
	if (K == EKeys::Down || K == EKeys::S || K == EKeys::Gamepad_DPad_Down) { Highlight = (Highlight + 1) % N; return FReply::Handled(); }
	if (K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::E || K == EKeys::Gamepad_FaceButton_Bottom) { Choose(Highlight); return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SRPGPauseMenu::OnMouseWheel(const FGeometry& G, const FPointerEvent& E)
{
	const int32 N = Items().Num();
	Highlight = (Highlight + (E.GetWheelDelta() > 0.f ? N - 1 : 1)) % N;
	return FReply::Handled();
}

void SRPGPauseMenu::Tick(const FGeometry& G, const double Time, const float Dt)
{
	SCompoundWidget::Tick(G, Time, Dt);
	SetRenderOpacity(Fx.Tick(Dt));
	// Modal: keep keyboard focus while shown (a click elsewhere mustn't strand the keys).
	if (GetVisibility() == EVisibility::Visible && !HasKeyboardFocus()) FSlateApplication::Get().SetKeyboardFocus(AsShared(), EFocusCause::SetDirectly);
}

#undef LOCTEXT_NAMESPACE
