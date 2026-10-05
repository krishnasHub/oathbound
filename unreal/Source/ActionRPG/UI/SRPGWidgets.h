#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UWorld;
class SVerticalBox;
class SHorizontalBox;
class SBox;

/*
 * All screen-space UI, built in C++ with Slate (no UMG assets):
 *   SRPGHud         vitals, ability bar, XP, gold, quest tracker, toasts, boss bar, death screen
 *   SRPGDialogue    speaker, text, choices ([Verb] tags in class colour); keys 1-9, Esc
 *   SRPGCharSelect  4 class cards, attribute + derived stat bars, weapons/defense/dialogue/abilities, sex
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
	virtual void Tick(const FGeometry& G, const double Time, const float Dt) override;

private:
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<SVerticalBox> Choices;
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
	SLATE_END_ARGS()
	void Construct(const FArguments& Args);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;

private:
	void Select(const FString& InClass) { ClassId = InClass; Rebuild(); OnPreview.ExecuteIfBound(ClassId, Sex); }
	void SetSex(const FString& InSex) { Sex = InSex; Rebuild(); OnPreview.ExecuteIfBound(ClassId, Sex); }
	void Rebuild();
	TWeakObjectPtr<UWorld> World;
	FOnBegin OnBegin;
	FOnPreview OnPreview;
	FString ClassId = TEXT("knight"), Sex = TEXT("male");
	TArray<FString> ClassIds;
	TSharedPtr<SBox> Body;
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
