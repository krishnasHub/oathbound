#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"

class UWorld;
class SVerticalBox;
class SHorizontalBox;
class SBox;
class SImage;
class STextBlock;
class UTexture2D;
class UMaterialInstanceDynamic;

/*
 * All screen-space UI, built in C++ with Slate (no UMG assets):
 *   SRPGHud         vitals, ability bar, XP, gold, quest tracker, toasts, boss bar, death screen
 *   SRPGDialogue    speaker, text, choices ([Verb] tags in class colour); keys 1-9, Esc
 *   SRPGTitle       title screen: Start New Game / Quit
 *   SRPGCharSelect  the hero animated on the left (every animation + ability effects), class / sex / stats / kit on the right
 *   SRPGPauseMenu   Resume / New Game / Quit
 *   SRPGPanel       Inventory / Character / Quests / Help
 */

/**
 * How every menu reacts to a choice: the highlighted item is blue (hover, arrows, wheel); choosing it turns it
 * gold for a moment, the menu fades out, then the choice happens (and a menu that stays up fades back in).
 * Input is ignored while that plays, so one click is one choice.
 */
struct FRPGChoose
{
	static FLinearColor Idle() { return FLinearColor(0.12f, 0.13f, 0.17f); }
	static FLinearColor Highlighted() { return FLinearColor(0.09f, 0.2f, 0.46f); }
	static FLinearColor Chosen() { return FLinearColor(0.9f, 0.56f, 0.06f); }
	/** Button colour for item I given the highlighted item. */
	FLinearColor Color(int32 I, int32 Highlight) const { return Item == I ? Chosen() : Highlight == I ? Highlighted() : Idle(); }

	bool Busy() const { return Item >= 0; }
	void Start(int32 InItem, TFunction<void()> InThen) { if (Busy()) return; Item = InItem; T = 0.f; Then = MoveTemp(InThen); }
	/** Advance; returns the menu's opacity for this frame. */
	float Tick(float Dt)
	{
		constexpr float Flash = 0.16f, Fade = 0.22f, FadeIn = 0.18f;
		if (Busy())
		{
			T += Dt;
			if (T < Flash) return 1.f;
			if (T < Flash + Fade) return 1.f - (T - Flash) / Fade;
			TFunction<void()> Run = MoveTemp(Then);
			Item = -1;
			InT = 0.f;
			if (Run) Run();
			return 0.f;
		}
		InT = FMath::Min(InT + Dt, FadeIn);
		return InT / FadeIn;
	}
	/** Restart the fade-in (the menu was just shown). */
	void Reset() { Item = -1; Then = nullptr; InT = 0.f; }

	int32 Item = -1;
	float T = 0.f, InT = 1.f;
	TFunction<void()> Then;
};

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
/**
 * Deep night (drawn under the HUD): near-black everywhere except around the hero and around fires / torches,
 * which keep their own pools of light. A UI material fed screen-space ellipses (the world circles projected,
 * so the camera's perspective is right).
 */
/** The game's own mouse cursor (drawn above all the UI): what a click would do - attack with this class's weapon,
 *  talk, or just walk (ARPGPlayerCharacter::CursorIcon). Hidden whenever the OS cursor is (menus etc.). */
class SRPGCursor : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGCursor) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1, 1); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<UWorld> World;
	TMap<FName, FSlateBrush> Brushes;
	TArray<TStrongObjectPtr<UTexture2D>> Keep;
};

class SRPGNightShade : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGNightShade) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1, 1); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;

private:
	TWeakObjectPtr<UWorld> World;
	TStrongObjectPtr<UMaterialInstanceDynamic> Mat;
	FSlateBrush Brush;
	float Strength = 0.f;
};

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

class SRPGDialogue : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGDialogue) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	void Refresh();
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	/** Where choice I is on screen (desktop pixels), for tests that click it. */
	FVector2D ChoiceScreenCenter(int32 I) const;

private:
	/** Move the highlighted choice by Step, skipping disabled ones (mouse wheel, arrows, W/S, d-pad). */
	void MoveHighlight(int32 Step);
	/** Choose a reply (click, 1-9, Enter): gold flash, fade, then the story moves on. */
	void Confirm(int32 Index);
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<SVerticalBox> Choices;
	int32 Highlight = 0;
	FRPGChoose Fx;
	// Portraits: the NPC on the left, the hero (mirrored, facing them) on the right; they slide up as it opens.
	FSlateBrush NpcBrush, HeroBrush;
	TStrongObjectPtr<UTexture2D> NpcTex, HeroTex;
	TSharedPtr<SWidget> NpcPortrait, HeroPortrait;
	TSharedPtr<SWidget> DialogBox;     // the darkness behind the conversation is centred on it
	float FadeIn = 0.f;
	FSlateBrush FadeBrush;
	TStrongObjectPtr<UTexture2D> FadeTex;
	float Appear = 0.f;
	bool bWasOpen = false;
	void SetPortrait(FSlateBrush& Brush, TStrongObjectPtr<UTexture2D>& Keep, const class ARPGCharacterBase* Who);
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
	FRPGChoose Fx;   // Begin (item 1) / Back (item 0)
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

/**
 * Title screen: the game's name over a slow drifting view of the world, Start New Game / Quit
 * (mouse, arrows / W S / wheel + Enter, d-pad + A). No saves yet.
 */
class SRPGTitle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGTitle) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		SLATE_EVENT(FSimpleDelegate, OnStart)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;

private:
	FSimpleDelegate OnStart, OnQuit;
	FRPGChoose Fx;
	FSlateBrush FadeBrush;
	TStrongObjectPtr<UTexture2D> FadeTex;
	int32 Highlight = 0;
	float Clock = 0.f;
	void Activate(int32 Index);
};

/**
 * Pause menu (Esc in game): Resume / New Game / Quit Game, over a dimmed, paused world. New Game and Quit
 * ask to confirm (there are no saves yet). Mouse, arrows / W S / wheel + Enter, Esc to go back; d-pad + A / B.
 */
class SRPGPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRPGPauseMenu) {}
		SLATE_EVENT(FSimpleDelegate, OnResume)
		SLATE_EVENT(FSimpleDelegate, OnNewGame)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	/** Show the main page with Resume highlighted. */
	void Open();
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
	virtual FReply OnMouseWheel(const FGeometry& G, const FPointerEvent& E) override;
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;

private:
	enum class EPage : uint8 { Main, ConfirmNew, ConfirmQuit };
	EPage Page = EPage::Main;
	int32 Highlight = 0;
	FRPGChoose Fx;
	void Choose(int32 Index) { Fx.Start(Index, [this, Index]() { Activate(Index); }); }
	FSimpleDelegate OnResume, OnNewGame, OnQuit;
	TSharedPtr<SVerticalBox> List;
	TArray<FString> Items() const;
	void Rebuild();
	void Activate(int32 Index);
	void Back();
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
