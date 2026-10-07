#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "TSChoose.h"

class UWorld;
class SVerticalBox;
class SHorizontalBox;
class SBox;
class SImage;
class STextBlock;
class UTexture2D;
class UMaterialInstanceDynamic;

/*
 * This game's screen-space UI, built in C++ with Slate (no UMG assets), on Tessera's UI kit (TesseraUI: the
 * dialogue box, title, pause menu, cursor, night shade, toasts, ability picker, FTSChoose, TSUI helpers):
 *   SRPGHud         vitals, ability bar, XP, gold, quest tracker, toasts, boss bar, minimap, death screen
 *   SRPGCharSelect  the hero animated on the left (every animation + ability effects), class / sex / stats / kit on the right
 *   SRPGPanel       Inventory / Character / Quests / Help
 */

class SRPGHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);

private:
	TWeakObjectPtr<UWorld> World;
	TSharedRef<SWidget> AbilitySlot(int32 Index);
	FSlateBrush VignetteBrush;            // low-health red edge
	TStrongObjectPtr<UTexture2D> VignetteTex;
};

/**
 * Minimap (bottom right): the map around the hero, north up, in a frame shaped by class - a glass orb (Mage),
 * a shield (Knight), a gold coin (Thief), an open book (Scholar). Dots: you (with a facing tick), foes (red,
 * orange while neutral), villagers (yellow), anyone with something for you (bigger, gold).
 */
class SRPGMinimap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGMinimap) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;

private:
	/** Is a point (-1..1 across the widget) inside the frame's shape? */
	bool Inside(const FVector2D& P) const;
	void SetShape(const FString& InShape);
	TWeakObjectPtr<UWorld> World;
	float Size = 230.f;
	float ViewTiles = 18.f;        // map tiles across the window
	FString Shape;
	FVector PlayerAt = FVector::ZeroVector;
	TStrongObjectPtr<UMaterialInstanceDynamic> Mat;
	TStrongObjectPtr<UTexture2D> FrameTex, MaskTex;
	FSlateBrush MapBrush, FrameBrush;
};

/**
 * Character select (after Start New Game): the hero big on the left, cycling through every animation it has
 * (idle, walking all four ways, its attacks, its defence - shield block, mana shield, bow - and each ability,
 * with a caption and a little effect), and the choices on the right: class cards with animated portraits, sex,
 * stats, kit and abilities (the one being shown lights up). 1-4 / M / F / Enter, Esc to go back.
 */
/**
 * Character-select backdrop: a little scene per class playing behind the hero, building up beat by beat over a
 * 14 s loop (restarts when the class changes). Drawn in backdrop pixels (BD_<class> is 240 x 200, floor at y 160),
 * anchored so the floor meets the hero's feet.
 *   knight   castle at dawn: light rays, then rose petals drifting down, then gold glints
 *   thief    moonlit rooftops: ninjas running and leaping across, then a chest bursts open, coins flying
 *   mage     storm: lightning strikes, then a rune circle kindles behind the hero, sparks rise, orbs circle
 *   scholar  library: dust in a sunbeam, a book opens on the lectern, its pages flutter out
 */
class SRPGBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGBackdrop) {}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, Hero)   // the hero image: the scene is laid out around its feet
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void SetScene(const FString& ClassId);
	void SetHero(const TSharedPtr<SWidget>& InHero) { Hero = InHero; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100, 100); }
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	const FSlateBrush* Brush(const FString& Texture, int32 Frames = 1, int32 Frame = 0) const;
	TWeakPtr<SWidget> Hero;
	FString Scene;
	float T = 0.f;
	mutable TMap<FString, TArray<FSlateBrush>> Brushes;   // per texture: one brush per frame (UV region)
	mutable TArray<TStrongObjectPtr<UTexture2D>> Keep;
};

class SRPGCharSelect : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnBegin, const FString& /*Class*/, const FString& /*Sex*/);
	DECLARE_DELEGATE_TwoParams(FOnPreview, const FString& /*Class*/, const FString& /*Sex*/);
	SLATE_BEGIN_ARGS(SRPGCharSelect) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		SLATE_EVENT(FOnBegin, OnBegin)
		SLATE_EVENT(FOnPreview, OnPreview)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;
	/** Show a class (as if its card was clicked). */
	void ShowClass(const FString& InClass) { if (ClassIds.Contains(InClass)) Select(InClass); }

private:
	/** One beat of the showcase: a sheet row/frame pattern, a caption, and an optional effect. */
	struct FStep
	{
		FString Caption, Detail;
		int32 Dir = 0;           // 0 facing the camera, 1 away, 2 side
		int32 Act = 0;           // 0 idle, 1 walk, 2 attack, 3 hurt, 4 guard
		bool bFlip = false;
		float Time = 1.5f;
		FName Fx;                // orb, arrow, ring, bubble, heal, block
		FLinearColor FxColor = FLinearColor::White;
		int32 Ability = -1;      // which ability card to light up
	};
	void Select(const FString& InClass);
	void SetSex(const FString& InSex);
	void Rebuild();
	void BuildShowcase();
	FString Sheet(const FString& Class) const;
	/** Point a brush at /Game/RPG/Pixel/<Texture>, drawn at Size (pixel art, nearest-filtered). */
	void SetBrush(FSlateBrush& B, const FString& Texture, const FVector2D& Size);
	static void SetFrame(FSlateBrush& B, int32 Row, int32 Col);

	TWeakObjectPtr<UWorld> World;
	FOnBegin OnBegin;
	FOnPreview OnPreview;
	FSimpleDelegate OnBack;
	FString ClassId = TEXT("knight"), Sex = TEXT("male");
	TArray<FString> ClassIds;
	TSharedPtr<SBox> Right;
	FTSChoose Fx;   // Begin (item 1) / Back (item 0)
	int32 HoverButton = -1;

	TArray<FStep> Steps;
	int32 StepIdx = 0;
	float StepT = 0.f, Clock = 0.f;
	FSlateBrush HeroBrush, FxBrush, StageBrush, ShadowBrush;
	FSlateBrush CardBrushes[4];
	TMap<FString, FSlateBrush> Icons;
	TArray<TStrongObjectPtr<UTexture2D>> Keep;   // brushes don't keep their textures alive
	TSharedPtr<SImage> HeroImage, FxImage, StageImage;
	TSharedPtr<SRPGBackdrop> Backdrop;
	TSharedPtr<STextBlock> PopText;
};

class SRPGPanel : public SCompoundWidget
{
public:
	DECLARE_DELEGATE(FOnClose);
	SLATE_BEGIN_ARGS(SRPGPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		SLATE_EVENT(FOnClose, OnClose)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Show(FName Mode);
	void Rebuild();
	FName Mode;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;

private:
	TWeakObjectPtr<UWorld> World;
	FOnClose OnClose;
	TSharedPtr<SBox> Body;
};
