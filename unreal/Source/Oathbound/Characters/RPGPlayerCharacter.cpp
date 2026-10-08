#include "RPGPlayerCharacter.h"
#include "TSInteractable.h"
#include "TSChannel.h"
#include "RPGTheft.h"
#include "TSFeedback.h"
#include "TSSky.h"
#include "Oathbound.h"
#include "TSData.h"
#include "RPGAssets.h"
#include "TSCombat.h"
#include "RPGSession.h"
#include "LMStory.h"
#include "RPGEnemy.h"
#include "TSFX.h"
#include "TSProjectile.h"
#include "TSInventory.h"
#include "TSAbilities.h"
#include "TSPoseMesh.h"
#include "TSLook.h"
#include "TSSprite.h"
#include "TSPerception.h"
#include "TSCameraRig.h"
#include "TSHeroControl.h"
#include "TSDayNight.h"
#include "Components/PointLightComponent.h"
#include "RPGWorldBuilder.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ARPGPlayerCharacter::ARPGPlayerCharacter()
{
	Team = ETSTeam::Player;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->TargetArmLength = 430.f;
	CameraBoom->SocketOffset = FVector(0, 60, 75);
	CameraBoom->ProbeSize = 14.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->FieldOfView = 80.f;
	Rig = CreateDefaultSubobject<UTSCameraRig>(TEXT("Rig"));
	Control = CreateDefaultSubobject<UTSHeroControl>(TEXT("Control"));

	Inventory = CreateDefaultSubobject<UTSInventoryComponent>(TEXT("Inventory"));
	Channel = CreateDefaultSubobject<UTSChannel>(TEXT("Channel"));
	Abilities = CreateDefaultSubobject<UTSAbilityComponent>(TEXT("Abilities"));

	PoseMesh = CreateDefaultSubobject<UTSPoseMesh>(TEXT("PoseMesh"));
	PoseMesh->SetupAttachment(GetCapsuleComponent());
	PoseMesh->SetRelativeLocationAndRotation(FVector(0, 0, -90.f), FRotator(0, -90.f, 0));
	// Guard: upper arm forward and down, forearm angled up across the chest, outer forearm (and the
	// shield strapped to it) facing forward. Directions are in character space: X fwd, Y right, Z up.
	PoseMesh->OnPosed = [this]() { UpdateBowString(); };
	PoseMesh->Poses.Add(TEXT("guard"), {
		{ TEXT("upperarm_l"), FVector(0.55f, 0.12f, -0.83f), FVector(0.f, -1.f, 0.f) },
		{ TEXT("lowerarm_l"), FVector(0.22f, 0.66f, 0.72f), FVector(1.f, 0.f, 0.f) },
	});
	// Mage cast: elbow in front of the ribs, hand out at chest height, wrist turned so the staff stands
	// up in front of the body (the staff runs along hand_r's X, top toward +X). Right-arm bones point back
	// toward the shoulder, so their targets are the negated limb directions.
	PoseMesh->Poses.Add(TEXT("cast"), {
		{ TEXT("upperarm_r"), -FVector(0.6f, 0.25f, -0.75f), FVector(0.f, -1.f, 0.f) },
		{ TEXT("lowerarm_r"), -FVector(0.9f, -0.15f, 0.4f), FVector(0.f, -1.f, 0.f) },
		{ TEXT("hand_r"), FVector(0.2f, -0.05f, 0.98f), FVector(0.f, -1.f, 0.f) },
	});
	// ...and the thrust as a bolt leaves: arm punched out, staff head tipped toward the target.
	PoseMesh->Poses.Add(TEXT("cast_kick"), {
		{ TEXT("upperarm_r"), -FVector(0.88f, 0.15f, -0.45f), FVector(0.f, -1.f, 0.f) },
		{ TEXT("lowerarm_r"), -FVector(0.97f, -0.12f, 0.2f), FVector(0.f, -1.f, 0.f) },
		{ TEXT("hand_r"), FVector(0.65f, -0.05f, 0.76f), FVector(0.f, -1.f, 0.f) },
	});
	// Bow arm straight out toward the target, hand turned so the bow stands upright.
	PoseMesh->Poses.Add(TEXT("bow_aim"), {
		{ TEXT("upperarm_l"), FVector(0.96f, -0.18f, 0.18f), FVector(0.f, 0.f, 1.f) },
		{ TEXT("lowerarm_l"), FVector(1.f, -0.04f, 0.1f), FVector(0.f, 0.f, 1.f) },
		{ TEXT("hand_l"), FVector(1.f, 0.f, 0.06f), FVector(0.f, 0.f, 1.f) },
	});
	// String hand drawn back to the cheek, elbow high and behind the shoulder.
	PoseMesh->Poses.Add(TEXT("bow_draw"), {
		{ TEXT("upperarm_r"), FVector(0.62f, -0.55f, -0.15f), FVector(0.f, 0.f, -1.f) },
		{ TEXT("lowerarm_r"), FVector(-0.8f, 0.56f, -0.12f), FVector(0.f, 0.f, -1.f) },
	});
	// Release: the string hand snaps back past the ear.
	PoseMesh->Poses.Add(TEXT("bow_release"), {
		{ TEXT("upperarm_r"), FVector(0.78f, -0.5f, -0.2f), FVector(0.f, 0.f, -1.f) },
		{ TEXT("lowerarm_r"), FVector(-0.55f, 0.75f, -0.35f), FVector(0.f, 0.f, -1.f) },
	});

	auto MakeFX = [this](const TCHAR* Name, const TCHAR* Shape)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		C->SetStaticMesh(ConstructorHelpers::FObjectFinder<UStaticMesh>(Shape).Object);
		return C;
	};
	// The Mage's barrier: a glowing column around the hero (the rim-lit material shows its sides, the caps stay faint).
	ShieldBubble = MakeFX(TEXT("ShieldBubble"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	GuardArc = MakeFX(TEXT("GuardArc"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	GuardArc->SetRelativeLocationAndRotation(FVector(55, 0, 0), FRotator(90, 0, 0));
	GuardArc->SetRelativeScale3D(FVector(1.1f, 1.1f, 0.03f));
	AimLine = MakeFX(TEXT("AimLine"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ARPGPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	Rig->Setup(CameraBoom, Camera);
	bTopDown = Rig->IsTopDown();

	// Mouse control: this class supplies the rules and the attacks.
	Control->TalkRange = TalkRange();
	Control->InAttackRange = [this](const ATSCharacter* T) { return InAttackRange(T); };
	Control->IsAttacking = [this]() { return bAttacking; };
	Control->Attack = [this]() { PrimaryAttack(); };
	// A steal walks right up to the mark (a talk stops at speaking distance).
	Control->ApproachRange = [this](const ATSCharacter* C) { return C && IsStealing() ? FMath::Max(0.f, RPGTheft::Reach(this, C) * 0.75f - C->Radius()) : -1.f; };
	Control->Interesting = [this](const ATSCharacter* C) { return Control->IsTalkMode() && IsSneaking() && RPGTheft::Pockets(C).IsValid(); };
	// An action-mode click by a crouched Thief steals instead of talking (walk up, then lift).
	Control->TalkBlocker = [this](const ATSCharacter* C)
	{
		if (!IsStealing()) return TalkBlocker(C);
		const FString Why = RPGTheft::Why(this, C, /*bIgnoreReach*/ true);
		if (!Why.IsEmpty()) bActionClick = false;   // refused: whatever comes next is an ordinary move
		return Why;
	};
	Control->Talk = [this](ATSCharacter* C)
	{
		if (IsStealing()) { bActionClick = false; RPGTheft::Begin(this, C); return; }
		URPGSession::Get(this)->OpenDialogue(C);
	};
	Control->UseBlocker = [this](const ATSInteractable* It) { return It && It->CanUse() ? FString() : FString(TEXT("...")); };
	Control->Use = [this](ATSInteractable* It) { const bool bKey = bUseByKey; bUseByKey = false; URPGSession::Get(this)->UseInteractable(It, bKey); };

	// After dark a soft, warm light follows the hero (faded in by the day/night cycle) so you never lose yourself.
	NightGlow = NewObject<UPointLightComponent>(this, TEXT("NightGlow"));
	NightGlow->SetupAttachment(RootComponent);
	NightGlow->SetRelativeLocation(FVector(0, 0, 260.f));
	NightGlow->SetIntensityUnits(ELightUnits::Candelas);
	NightGlow->SetIntensity(0.f);
	NightGlow->SetAttenuationRadius(1100.f);
	NightGlow->SetLightColor(FLinearColor(0.62f, 0.74f, 1.f));   // cool, moonlit: the night around you reads blue
	NightGlow->SetCastShadows(false);
	NightGlow->RegisterComponent();
	// Nightfall is an event (Tessera's sky announces it); the lights here are this game's answer to it.
	if (UTSDayNight* DayNight = UTSDayNight::Get(this)) DayNight->OnNightLevel.AddUObject(this, &ARPGPlayerCharacter::OnNightLevel);
	BuildOrbLight();

	ComboMontage = TSAssets::Load<UAnimMontage>(RPGAssets::ComboMontage);
	if (ComboMontage) for (const FCompositeSection& S : ComboMontage->CompositeSections) ComboSections.Add(S.SectionName);

	UMaterialInterface* Fresnel = TSAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Fresnel.M_RPG_Fresnel"));
	BubbleMat = UMaterialInstanceDynamic::Create(Fresnel, this);
	BubbleMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.65f, 1.f));
	ShieldBubble->SetMaterial(0, BubbleMat);
	GuardMat = UMaterialInstanceDynamic::Create(Fresnel, this);
	GuardArc->SetMaterial(0, GuardMat);
	AimMat = UMaterialInstanceDynamic::Create(TSAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Telegraph.M_RPG_Telegraph")), this);
	AimMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.93f, 0.7f));
	AimLine->SetMaterial(0, AimMat);

	SpawnPoint = GetActorLocation();
	if (!ClassDef.IsValid()) ApplyClass(ClassId, Sex);
}

TSJson::FObj ARPGPlayerCharacter::Style() const
{
	const TArray<TSharedPtr<FJsonValue>> Styles = TSJson::Arr(ClassDef, TEXT("styles"));
	if (Styles.IsEmpty()) return nullptr;
	const FString Id = Styles[FMath::Clamp(StyleIndex, 0, Styles.Num() - 1)]->AsString();
	return UTSData::Get(this).Entry(TEXT("weaponStyles"), Id);
}

void ARPGPlayerCharacter::ApplyClass(const FString& InClassId, const FString& InSex)
{
	const UTSData& D = UTSData::Get(this);
	ClassId = InClassId;
	Sex = InSex;
	ClassDef = D.Entry(TEXT("classes"), ClassId);
	StyleIndex = 0;
	DisplayName = TSJson::Str(ClassDef, TEXT("name"));
	NameColor = TSJson::Color(TSJson::Str(ClassDef, TEXT("color")));

	const TSJson::FObj PlayerLook = TSJson::Obj(TSJson::Obj(D.World(), TEXT("looks")), TEXT("player"));
	SetLook(TSJson::Str(PlayerLook, Sex, Sex == TEXT("female") ? TEXT("quinn") : TEXT("manny")), FString(), 1.f);
	UseSprite(ClassId + (Sex == TEXT("female") ? TEXT("_f") : TEXT("_m")));
	// Render the posed copy; the animated mesh keeps animating (and firing notifies) while hidden.
	PoseMesh->SetSkinnedAssetAndUpdate(GetMesh()->GetSkeletalMeshAsset());
	PoseMesh->Source = GetMesh();
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetMesh()->SetVisibility(false, false);

	// Stats, as the prototype's classBaseStats().
	const TSJson::FObj PlayerDef = D.Section(TEXT("player"));
	Stats->Base.Reset();
	const TSJson::FObj Attrs = TSJson::Obj(ClassDef, TEXT("attributes"));
	if (Attrs.IsValid()) for (const auto& KV : Attrs->Values) Stats->Base.Add(FName(*KV.Key), float(KV.Value->AsNumber()));
	Stats->Base.Add(TEXT("hpFlat"), float(TSJson::Num(ClassDef, TEXT("hpBase"), 60)));
	Stats->Base.Add(TEXT("manaFlat"), float(TSJson::Num(ClassDef, TEXT("manaBase"), 30)));
	Stats->Base.Add(TEXT("manaRegen"), float(TSJson::Num(ClassDef, TEXT("manaRegen"), D.Tuning(TEXT("manaRegen"), 2.5))));
	Stats->Base.Add(TEXT("level"), 1.f);
	Stats->Base.Add(TEXT("critPct"), float(TSJson::Num(ClassDef, TEXT("critPct"), 5)));
	Stats->Base.Add(TEXT("weaponDamage"), float(TSJson::Num(PlayerDef, TEXT("unarmedDamage"), 4)));
	Stats->Effects.Reset();
	MaxPoise = Poise = float(TSJson::Num(PlayerDef, TEXT("poise"), 60));
	HitIframes = float(D.Tuning(TEXT("playerHitIframes"), 0.45));
	Xp = 0;
	AttrPoints = 0;

	// Starting gear.
	for (const FString& Slot : { TEXT("weapon"), TEXT("armor"), TEXT("trinket") }) Stats->RemoveModifiers(FName(Slot));
	Inventory->Items.Reset();
	Inventory->Equipment.Reset();
	Inventory->Capacity = int32(TSJson::Num(PlayerDef, TEXT("inventorySize"), 16));
	Inventory->Currency = int32(TSJson::Num(PlayerDef, TEXT("startGold"), 10));
	for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(ClassDef, TEXT("startItems")))
	{
		const TSJson::FObj S = V->AsObject();
		FTSItem It = UTSInventoryComponent::MakeItem(this, TSJson::Str(S, TEXT("item")));
		It.Qty = int32(TSJson::Num(S, TEXT("qty"), 1));
		Inventory->Add(It);
		if (TSJson::Bool(S, TEXT("equip"))) Inventory->Equip(It.Uid);
	}
	Stats->Fill();

	TArray<FString> AbilityIds;
	for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(ClassDef, TEXT("abilities"))) AbilityIds.Add(V->AsString());
	Abilities->Setup(AbilityIds);

	Tags.Remove(TEXT("Sneaking"));
	UpdateSpeed();
	// Night eyes (classes.<id>.nightEyes): the Thief makes out shapes in the dark beyond the light.
	ATSSky::SetNightEyes(float(TSJson::Num(ClassDef, TEXT("nightEyes"), 1.0)));
	RefreshWeapons();
	UE_LOG(LogRPG, Display, TEXT("Player is a %s %s: HP %.0f, stamina %.0f, mana %.0f, speed %.0f uu/s."),
		*Sex, *ClassId, Stats->MaxHealth(), Stats->Max(RPGStat::Stamina), Stats->Max(RPGStat::Mana), GetCharacterMovement()->MaxWalkSpeed);
}

int32 ARPGPlayerCharacter::XpToNext(const UObject* Ctx, int32 InLevel)
{
	const UTSData& D = UTSData::Get(Ctx);
	return FMath::RoundToInt(D.Tuning(TEXT("xpBase"), 40) * FMath::Pow(D.Tuning(TEXT("xpGrowth"), 1.45), InLevel - 1));
}

void ARPGPlayerCharacter::GainXp(int32 Amount)
{
	if (Amount <= 0) return;
	URPGSession* Session = URPGSession::Get(this);
	Xp += Amount;
	UTSFeedback::Get(Session)->Float(Head() + FVector(0, 0, 50), FString::Printf(TEXT("+%d XP"), Amount), FLinearColor(0.7f, 0.55f, 1.f), 0.8f);
	while (Xp >= XpToNext(this, Level()))
	{
		Xp -= XpToNext(this, Level());
		Stats->Base.FindOrAdd(TEXT("level")) += 1.f;
		const int32 Points = int32(UTSData::Get(this).Tuning(TEXT("pointsPerLevel"), 3));
		AttrPoints += Points;
		Stats->Fill();
		ATSFX::Ring(GetWorld(), GetActorLocation() - FVector(0, 0, 86), 220.f, FLinearColor(1.f, 0.83f, 0.3f), 0.8f);
		UTSFeedback::Get(Session)->Toast(FString::Printf(TEXT("Level %d! +%d attribute points — press C"), Level(), Points), FLinearColor(1.f, 0.83f, 0.3f));
		for (int32 I = 0; I < Abilities->Ids.Num(); ++I)
		{
			const TSJson::FObj A = Abilities->Def(Abilities->Ids[I]);
			if (int32(TSJson::Num(A, TEXT("unlockLevel"), 1)) == Level())
				UTSFeedback::Get(Session)->Toast(FString::Printf(TEXT("New ability: %s [%d]"), *TSJson::Str(A, TEXT("name")), I + 1), TSJson::Color(TSJson::Str(A, TEXT("color"))));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Input — actions and the mapping context are created here, not as .uasset files
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::CreateInput()
{
	if (InputContext) return;
	InputContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Default"));

	auto Make = [this](FName Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, *(TEXT("IA_") + Name.ToString()));
		A->ValueType = Type;
		Actions.Add(Name, A);
		return A;
	};
	UInputAction* Move = Make(TEXT("Move"), EInputActionValueType::Axis2D);
	auto MapMove = [&](FKey Key, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& M = InputContext->MapKey(Move, Key);
		if (bSwizzle) M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		if (bNegate) M.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	};
	MapMove(EKeys::W, true, false); MapMove(EKeys::S, true, true);
	MapMove(EKeys::D, false, false); MapMove(EKeys::A, false, true);

	InputContext->MapKey(Make(TEXT("Look"), EInputActionValueType::Axis2D), EKeys::Mouse2D);
	InputContext->MapKey(Make(TEXT("Zoom"), EInputActionValueType::Axis1D), EKeys::MouseWheelAxis);
	InputContext->MapKey(Make(TEXT("Attack"), EInputActionValueType::Boolean), EKeys::LeftMouseButton);
	InputContext->MapKey(Make(TEXT("Secondary"), EInputActionValueType::Boolean), EKeys::RightMouseButton);
	// Top-down: Space dodges (Shift is the attack-in-place modifier) and there's no jump.
	const bool bTD = UTSCameraRig::IsTopDown(this);
	InputContext->MapKey(Make(TEXT("Dodge"), EInputActionValueType::Boolean), bTD ? EKeys::SpaceBar : EKeys::LeftShift);
	UInputAction* JumpAction = Make(TEXT("Jump"), EInputActionValueType::Boolean);
	if (!bTD) InputContext->MapKey(JumpAction, EKeys::SpaceBar);
	// Top-down: Shift + wheel opens the ability picker; letting go of Shift casts the highlighted one.
	UInputAction* PickAction = Make(TEXT("Pick"), EInputActionValueType::Boolean);
	if (bTD) InputContext->MapKey(PickAction, EKeys::LeftShift);

	// Simple key presses, all routed through OnKey(Name).
	const TPair<FName, FKey> Keys[] = {
		{ TEXT("Interact"), EKeys::E }, { TEXT("Potion"), EKeys::Q }, { TEXT("Swap"), EKeys::X },
		{ TEXT("Ability1"), EKeys::One }, { TEXT("Ability2"), EKeys::Two }, { TEXT("Ability3"), EKeys::Three }, { TEXT("Ability4"), EKeys::Four },
		{ TEXT("Inventory"), EKeys::I }, { TEXT("Character"), EKeys::C }, { TEXT("Quests"), EKeys::J }, { TEXT("Help"), EKeys::H },
		{ TEXT("Escape"), EKeys::Escape }, { TEXT("Debug"), EKeys::Tilde },
		{ TEXT("CheatLevel"), EKeys::K }, { TEXT("CheatGold"), EKeys::G },
	};
	for (const TPair<FName, FKey>& K : Keys) InputContext->MapKey(Make(K.Key, EInputActionValueType::Boolean), K.Value);
	InputContext->MapKey(Actions[TEXT("Escape")], EKeys::Gamepad_Special_Right);   // a controller's Start button pauses too
}

void ARPGPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	CreateInput();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			Sub->AddMappingContext(InputContext, 0);
}

void ARPGPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PIC)
{
	Super::SetupPlayerInputComponent(PIC);
	CreateInput();
	UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PIC);
	EIC->BindAction(Actions[TEXT("Move")], ETriggerEvent::Triggered, this, &ARPGPlayerCharacter::OnMove);
	EIC->BindAction(Actions[TEXT("Move")], ETriggerEvent::Completed, this, &ARPGPlayerCharacter::OnMove);
	EIC->BindAction(Actions[TEXT("Look")], ETriggerEvent::Triggered, this, &ARPGPlayerCharacter::OnLook);
	EIC->BindAction(Actions[TEXT("Zoom")], ETriggerEvent::Triggered, this, &ARPGPlayerCharacter::OnZoom);
	EIC->BindAction(Actions[TEXT("Pick")], ETriggerEvent::Completed, this, &ARPGPlayerCharacter::OnPickReleased);
	EIC->BindAction(Actions[TEXT("Attack")], ETriggerEvent::Started, this, &ARPGPlayerCharacter::OnAttack);
	EIC->BindAction(Actions[TEXT("Attack")], ETriggerEvent::Completed, this, &ARPGPlayerCharacter::OnAttackReleased);
	EIC->BindAction(Actions[TEXT("Secondary")], ETriggerEvent::Started, this, &ARPGPlayerCharacter::OnSecondary);
	EIC->BindAction(Actions[TEXT("Secondary")], ETriggerEvent::Completed, this, &ARPGPlayerCharacter::OnSecondaryReleased);
	EIC->BindAction(Actions[TEXT("Dodge")], ETriggerEvent::Started, this, &ARPGPlayerCharacter::OnDodge);
	EIC->BindAction(Actions[TEXT("Jump")], ETriggerEvent::Started, this, &ARPGPlayerCharacter::OnJump);
	EIC->BindAction(Actions[TEXT("Jump")], ETriggerEvent::Completed, this, &ARPGPlayerCharacter::StopJumping);
	for (const auto& KV : Actions)
	{
		static const TSet<FName> Handled = { TEXT("Move"), TEXT("Look"), TEXT("Zoom"), TEXT("Pick"), TEXT("Attack"), TEXT("Secondary"), TEXT("Dodge"), TEXT("Jump") };
		if (!Handled.Contains(KV.Key)) EIC->BindAction(KV.Value, ETriggerEvent::Started, this, &ARPGPlayerCharacter::OnKey, KV.Key);
	}
}

void ARPGPlayerCharacter::TestPress(FName Action, bool bDown)
{
	// Scripted presses run the real handlers, but never read the real mouse cursor.
	const bool bLocked = Control->bInputLocked;
	TGuardValue<bool> NoCursor(Control->bScripted, true);
	Control->bInputLocked = false;
	if (Action == TEXT("Attack")) bDown ? OnAttack() : OnAttackReleased();
	else if (Action == TEXT("Secondary")) bDown ? OnSecondary() : OnSecondaryReleased();
	else if (Action == TEXT("Dodge")) { if (bDown) OnDodge(); }
	else if (Action == TEXT("Jump")) { bDown ? OnJump() : StopJumping(); }
	else if (bDown) OnKey(Action);
	Control->bInputLocked = bLocked;
}

void ARPGPlayerCharacter::OnJump()
{
	// A plain jump: the combat anim blueprint plays jump / fall / land from CharacterMovement state.
	if (Control->bInputLocked || bDead || bAttacking || IsDodging() || Tags.Has(TEXT("Staggered"))) return;
	Jump();
}

void ARPGPlayerCharacter::OnMove(const FInputActionValue& V) { MoveInput = Control->bInputLocked ? FVector2D::ZeroVector : V.Get<FVector2D>(); }

void ARPGPlayerCharacter::OnLook(const FInputActionValue& V)
{
	if (Control->bInputLocked || bTopDown) return;   // top-down: the mouse moves the cursor, not the camera
	const FVector2D L = V.Get<FVector2D>();
	AddControllerYawInput(L.X * 0.6f);
	AddControllerPitchInput(-L.Y * 0.6f);
}

void ARPGPlayerCharacter::OnZoom(const FInputActionValue& V)
{
	if (Control->bInputLocked || !bTopDown || bDead) return;
	// Shift + wheel: the ability picker. Plain wheel: zoom (within limits).
	const float Wheel = V.Get<float>();
	if (!Control->HandleWheel(Wheel)) Rig->Zoom(Wheel);
}

void ARPGPlayerCharacter::OnPickReleased() { if (!Control->bInputLocked) Control->ClosePicker(true); }

void ARPGPlayerCharacter::SetInputLocked(bool bLocked) { Control->bInputLocked = bLocked; }

void ARPGPlayerCharacter::ClearHeldInput()
{
	bGuardHeld = false;
	bDrawing = false;
	MoveInput = FVector2D::ZeroVector;
	Control->ClearHeldInput();
}

// ---------------------------------------------------------------------------------------------
// Cursor and talking (the clicks themselves are UTSHeroControl's)
// ---------------------------------------------------------------------------------------------

FName ARPGPlayerCharacter::CursorIcon() const
{
	FVector O, R;
	if (!Control->CursorRay(O, R) || bDead) return NAME_None;
	bool bHostile = false;
	const ATSCharacter* On = Control->UnderCursor(bHostile);
	if (Control->IsTalkMode())
	{
		const FName Icon = ActionIcon(On, On ? nullptr : Control->ObjectUnderCursor());
		// Something to do: the gears turn (two frames).
		if (Icon == TEXT("gear")) return FMath::Fmod(GetWorld()->GetRealTimeSeconds(), 0.36f) < 0.18f ? FName(TEXT("gear_a")) : FName(TEXT("gear_b"));
		return Icon;
	}
	// The weapon this class attacks with right now.
	auto Weapon = [this]() -> FName
	{
		if (bDrawing || BowOut > 0.f) return TEXT("arrow");
		if (TSJson::Str(TSJson::Obj(Style(), TEXT("primary")), TEXT("type")) == TEXT("bolt")) return TEXT("wand");
		return ClassId == TEXT("knight") ? FName(TEXT("sword")) : FName(TEXT("dagger"));
	};
	if (bDrawing) return TEXT("arrow");
	if (On) return bHostile ? Weapon() : FName(TEXT("talk"));
	if (Control->IsModifierDown() && Control->PickerSlot() < 0) return Weapon();   // attack in place
	return TEXT("pointer");
}

bool ARPGPlayerCharacter::IsStealing() const { return bActionClick && IsSneaking() && ClassId == TEXT("thief"); }

FName ARPGPlayerCharacter::ActionIcon(const ATSCharacter* On, const ATSInteractable* It) const
{
	if (On)
	{
		// Crouched, the Thief only steals (never talks): gears if it can't see you and has pockets left.
		if (IsSneaking() && ClassId == TEXT("thief"))
			return RPGTheft::Why(this, On, /*bIgnoreReach*/ true).IsEmpty() ? FName(TEXT("gear")) : FName(TEXT("gear_off"));
		return TalkBlocker(On).IsEmpty() ? FName(TEXT("talk")) : FName(TEXT("talk_off"));
	}
	if (It)
	{
		const URPGSession* S = URPGSession::Get(this);
		if (S->IsLock(It)) return S->IsLocked(It) && ClassId == TEXT("thief") ? FName(TEXT("gear")) : FName(TEXT("gear_off"));   // (beyond his level he can still try)
		return It->CanUse() ? FName(TEXT("gear")) : FName(TEXT("gear_off"));
	}
	return TEXT("gear_off");
}

FString ARPGPlayerCharacter::ActionLabel(const ATSCharacter* On, const ATSInteractable* It) const
{
	if (On)
	{
		if (IsSneaking() && ClassId == TEXT("thief"))
		{
			const FString No = RPGTheft::Why(this, On, true);
			return No.IsEmpty() ? FString::Printf(TEXT("Steal: %s (%s)"), *On->DisplayName, *RPGTheft::Hint(On)) : On->DisplayName + TEXT(": ") + No;
		}
		const FString No = TalkBlocker(On);
		return No.IsEmpty() ? TEXT("Talk: ") + On->DisplayName : On->DisplayName + TEXT(": ") + No;
	}
	if (It)
	{
		const URPGSession* S = URPGSession::Get(this);
		if (S->IsLock(It)) return !S->IsLocked(It) ? It->DisplayName + TEXT(" (open)") : ClassId == TEXT("thief") ? FString::Printf(TEXT("Pick the lock: %s (level %d)"), *It->DisplayName, int32(TSJson::Num(It->Def, TEXT("pickLevel"), 1))) : It->DisplayName + TEXT(": locked");
		return It->DisplayName;
	}
	return FString();
}

void ARPGPlayerCharacter::ActionClick(ATSCharacter* On)
{
	bActionClick = bUseByKey = true;
	Control->TryTalk(On);
}

float ARPGPlayerCharacter::TalkRange() const
{
	const UTSData& D = UTSData::Get(this);
	return D.Px(D.Tuning(TEXT("interactRange"), 48)) + 60.f;
}

bool ARPGPlayerCharacter::Speaks(const FString& Language) const
{
	if (Language.IsEmpty()) return false;
	for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(ClassDef, TEXT("languages")))
		if (V->AsString() == TEXT("*") || V->AsString() == Language) return true;
	return false;
}

FString ARPGPlayerCharacter::CantSpeakWhy(const UObject* WorldContext, const FString& Language)
{
	const FString Name = TSJson::Str(UTSData::Get(WorldContext).Entry(TEXT("languages"), Language), TEXT("name"), Language);
	return FString::Printf(TEXT("It speaks %s. You don't."), *Name);
}

FString ARPGPlayerCharacter::TalkBlocker(const ATSCharacter* C) const
{
	if (!C || C->IsDead() || C->IsLeaving()) return TEXT("...");
	if (C->IsFrozen()) return TEXT("Frozen solid.");
	// Language first: slimes have none; the dead and the old ones only talk to those who speak their tongue.
	const ARPGEnemy* E = Cast<ARPGEnemy>(C);
	const FString Lang = E ? E->Speaks() : FString(TEXT("common"));
	if (Lang.IsEmpty()) return TEXT("It can't be reasoned with.");
	if (!Speaks(Lang)) return CantSpeakWhy(this, Lang);
	if (C->DialogueRoot.IsEmpty()) return TEXT("They have nothing to say.");
	// Foes talk only while their faction is still neutral; mid-fight it takes Silver Words.
	if (C->Team == ETSTeam::Hostile && !C->IsPassive()) return TEXT("They're past talking.");
	return FString();
}

void ARPGPlayerCharacter::SnapCamera()
{
	CameraBoom->bEnableCameraLag = false;
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, [this]() { CameraBoom->bEnableCameraLag = true; }, 0.1f, false);
}

void ARPGPlayerCharacter::UpdateArea()
{
	const UTSData& D = UTSData::Get(this);
	const FTSArea* A = D.AreaAt(GetActorLocation());
	const FString Now = A ? A->Id : FString();
	if (Now == AreaId) return;
	AreaId = Now;
	// An area's look: darker, closed in (camera post-process), or back to the open sky.
	const TSJson::FObj Look = A ? TSJson::Obj(A->Def, TEXT("look")) : nullptr;
	for (TActorIterator<ATSSky> It(GetWorld()); It; ++It)
		It->SetIndoors(float(TSJson::Num(Look, TEXT("exposure"), 0)), float(TSJson::Num(Look, TEXT("vignette"), 0.3)));
	const FString Name = A ? A->Name : TSJson::Str(TSJson::Obj(D.Section(TEXT("map")), TEXT("main")), TEXT("name"));
	if (!Name.IsEmpty()) UTSFeedback::Get(this)->Toast(Name, FLinearColor(0.9f, 0.85f, 0.7f));
}

FRotator ARPGPlayerCharacter::MoveFrame() const
{
	if (bTopDown) return FRotator(0, CameraBoom->GetComponentRotation().Yaw, 0);
	return FRotator(0, Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw, 0);
}

void ARPGPlayerCharacter::OnKey(FName Key)
{
	if (Control->bInputLocked) return;
	URPGSession* Session = URPGSession::Get(this);
	if (Key == TEXT("Interact"))
	{
		// Top-down: E switches action mode on (the cursor shows what a click would do) or off.
		if (bTopDown) { Control->SetTalkMode(!Control->IsTalkMode()); return; }
		bUseByKey = true;
		if (ARPGCharacterBase* T = TalkTarget()) Session->OpenDialogue(T);
		else if (ATSInteractable* Near = ATSInteractable::Nearest(GetWorld(), GetActorLocation(), Control->TalkRange)) Session->UseInteractable(Near, true);
		return;
	}
	if (Key == TEXT("Potion")) { DrinkPotion(); return; }
	if (Key == TEXT("Swap")) { SwapStyle(); return; }
	if (Key == TEXT("Ability1")) { Abilities->TryActivate(0); return; }
	if (Key == TEXT("Ability2")) { Abilities->TryActivate(1); return; }
	if (Key == TEXT("Ability3")) { Abilities->TryActivate(2); return; }
	if (Key == TEXT("Ability4")) { Abilities->TryActivate(3); return; }
	if (Key == TEXT("Debug")) { Session->SetDebug(!Session->IsDebug()); return; }
	if (Key == TEXT("CheatLevel")) { if (Session->IsDebug()) GainXp(XpToNext(this, Level()) - Xp); return; }
	if (Key == TEXT("CheatGold")) { if (Session->IsDebug()) { Inventory->Currency += 100; Inventory->OnChanged.Broadcast(); } return; }
	if (Key == TEXT("Escape") && Control->CancelModes()) return;   // Esc backs out of the picker / talk mode first
	if (UIHandler) UIHandler(Key);   // Inventory / Character / Quests / Help / Escape (pause menu)
}

// ---------------------------------------------------------------------------------------------
// Aiming
// ---------------------------------------------------------------------------------------------

FVector ARPGPlayerCharacter::AimPoint(float MaxDistance) const
{
	if (bTopDown)
	{
		// The point under the cursor, level with the chest (where shots fly). Without a cursor (automated
		// runs) aim along the controller's yaw, which the self-tests set.
		if (const ATSCharacter* T = ClickedFoe()) return T->Chest();   // the enemy you clicked
		const FVector C = Chest();
		FVector OnCursor;
		if (Control->CursorAtHeight(C.Z, OnCursor)) return OnCursor;
		const float Yaw = Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw;
		return C + FRotator(0, Yaw, 0).Vector() * MaxDistance;
	}
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * MaxDistance;
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(PlayerAim), false, this);
	return GetWorld()->LineTraceSingleByChannel(H, Start, End, ECC_Visibility, Q) ? H.ImpactPoint : End;
}

FVector ARPGPlayerCharacter::AimDirection(const FVector& From) const
{
	// Aim assist: an opponent within ~12 degrees of the crosshair (and in sight) gets the shot.
	// Top-down with a cursor: the opponent the cursor is on (or nearest to it).
	if (const ATSCharacter* T = ClickedFoe()) return (T->Chest() - From).GetSafeNormal();
	FVector RayO, RayDir;
	if (Control->CursorRay(RayO, RayDir))
	{
		if (const ATSCharacter* T = Control->CursorAssist()) return (T->Chest() - From).GetSafeNormal();
	}
	else
	{
		// The crosshair (third person), or toward AimPoint (top-down without a cursor: automated runs).
		FVector CamPos = Camera->GetComponentLocation(), CamFwd = Camera->GetForwardVector();
		if (bTopDown) { CamPos = Chest(); CamFwd = (AimPoint() - CamPos).GetSafeNormal2D(); }
		const ATSCharacter* Best = nullptr;
		float BestDot = FMath::Cos(FMath::DegreesToRadians(12.f));
		for (ATSCharacter* E : TSCombat::Opponents(this))
		{
			if (FVector::Dist(E->Chest(), Chest()) > 3000.f) continue;
			const FVector To = E->Chest() - CamPos;
			const float Dot = FVector::DotProduct(CamFwd, To.GetSafeNormal());
			if (Dot <= BestDot) continue;
			FHitResult H;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(AimAssist), false, this);
			Q.AddIgnoredActor(E);
			if (GetWorld()->LineTraceSingleByChannel(H, Chest(), E->Chest(), ECC_Visibility, Q)) continue;
			BestDot = Dot;
			Best = E;
		}
		if (Best) return (Best->Chest() - From).GetSafeNormal();
	}

	// Otherwise toward the crosshair, but stay near level: a camera looking down at the ground
	// shouldn't bury shots in the dirt a few metres ahead, and one looking at the sky shouldn't lob them.
	FVector Dir = AimPoint() - From;
	Dir.Z = FMath::Clamp(Dir.Z, -Dir.Size2D() * 0.06f, Dir.Size2D() * 0.3f);
	return Dir.GetSafeNormal();
}

FVector ARPGPlayerCharacter::Muzzle() const
{
	if (const UStaticMeshComponent* Orb = KitGlow(TEXT("staff"))) return Orb->GetComponentLocation();
	if (BowOut > 0.f || bDrawing) return BodyMesh()->GetSocketLocation(TEXT("hand_l")) + FVector(0, 0, 4);
	return Chest() + GetActorForwardVector() * 50.f;
}

FVector ARPGPlayerCharacter::ArrowTarget(const FVector& From, float MaxRange) const
{
	// A foe along the aim (the clicked one, the one under the cursor, or aim assist): its chest.
	const FVector Dir = AimDirection(From);
	const ATSCharacter* Best = nullptr;
	float BestAlong = MaxRange;
	if (const ATSCharacter* T = ClickedFoe()) { Best = T; BestAlong = FVector::Dist2D(From, Best->Chest()); }
	else
	{
		for (ATSCharacter* E : TSCombat::Opponents(this))
		{
			const FVector To = E->Chest() - From;
			const float Along = FVector::DotProduct(To, Dir);
			if (Along <= 0.f || Along > BestAlong) continue;
			if ((To - Dir * Along).Size2D() > E->Radius() + 45.f) continue;
			Best = E;
			BestAlong = Along;
		}
	}
	if (Best && FVector::Dist2D(From, Best->Chest()) <= MaxRange) return Best->Chest();

	// Otherwise the ground under the cursor (or straight ahead), no further than the range.
	FVector Point = AimPoint();
	FVector Flat = FVector(Point.X - From.X, Point.Y - From.Y, 0.f);
	if (Flat.Size() > MaxRange || Flat.IsNearlyZero()) Flat = (Flat.IsNearlyZero() ? FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal() : Flat.GetSafeNormal()) * MaxRange;
	FVector Land = From + Flat;
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ArrowLand), false, this);
	Land.Z = GetWorld()->LineTraceSingleByChannel(H, Land + FVector(0, 0, 600.f), Land - FVector(0, 0, 1500.f), ECC_Visibility, Q)
		? H.ImpactPoint.Z : GetActorLocation().Z - GetSimpleCollisionHalfHeight();
	return Land;
}

void ARPGPlayerCharacter::PlayCast()
{
	CastKick = 0.16f;
	SpriteAttackAt = GetWorld()->GetTimeSeconds();
	CastHold = 0.7f;
	OrbFlash = 1.f;
}

void ARPGPlayerCharacter::PlayBowShot()
{
	BowRelease = 0.2f;
	SpriteAttackAt = GetWorld()->GetTimeSeconds();
	BowOut = 1.2f;
}

void ARPGPlayerCharacter::OnAbilityUsed(const TSJson::FObj& Ability)
{
	const FString Type = TSJson::Str(Ability, TEXT("type"));
	const bool bMage = TSJson::Str(TSJson::Obj(Style(), TEXT("primary")), TEXT("type")) == TEXT("bolt");
	const bool bBow = TSJson::Str(TSJson::Obj(Style(), TEXT("secondary")), TEXT("type")) == TEXT("bow");
	if (bMage && Type != TEXT("blink")) PlayCast();                      // Fireball, Frost Nova, Chain Lightning
	if (bBow && Type == TEXT("projectile")) PlayBowShot();                // Volley
}

void ARPGPlayerCharacter::UpdateBowString()
{
	UStaticMeshComponent* Rest = KitPart(TEXT("bow"), TEXT("string"));
	USceneComponent* Bow = KitRoot(TEXT("bow"));
	const bool bPull = Rest && Bow && bDrawing && !bDead;
	if (Rest) Rest->SetVisibility(!bPull);
	if (DrawParts.IsEmpty() && bPull)
	{
		const TCHAR* Parts[][2] = { { TEXT("Cylinder"), TEXT("M_Metal_Chrome") }, { TEXT("Cylinder"), TEXT("M_Metal_Chrome") },
		                            { TEXT("Cylinder"), TEXT("M_Wood_Walnut") }, { TEXT("Cone"), TEXT("M_Metal_Steel") } };
		for (const auto& P : Parts)
		{
			UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this);
			M->SetStaticMesh(TSAssets::Shape(P[0]));
			M->SetMaterial(0, RPGAssets::StarterMat(P[1]));
			M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			M->SetCastShadow(false);
			M->SetUsingAbsoluteLocation(true); M->SetUsingAbsoluteRotation(true); M->SetUsingAbsoluteScale(true);
			M->SetupAttachment(GetRootComponent());
			M->RegisterComponent();
			DrawParts.Add(M);
		}
	}
	for (UStaticMeshComponent* M : DrawParts) M->SetVisibility(bPull);
	if (!bPull) return;

	// String ends in the bow's weapon space (see the "bow" kit), the nock just in front of the fingers.
	const FTransform B = Bow->GetComponentTransform();
	const FVector Top = B.TransformPosition(FVector(-15, 0, 62)), Bottom = B.TransformPosition(FVector(-15, 0, -62));
	const FTransform H = PoseMesh->GetSocketTransform(TEXT("hand_r"));
	const FVector Nock = H.GetLocation() - H.GetUnitAxis(EAxis::X) * 8.f;   // hand_r's X points back up the arm
	auto Segment = [](UStaticMeshComponent* M, const FVector& A, const FVector& C, float Thick)
	{
		M->SetWorldLocationAndRotation((A + C) * 0.5f, FRotationMatrix::MakeFromZ(C - A).Rotator());
		M->SetWorldScale3D(FVector(Thick, Thick, FVector::Dist(A, C) / 100.f));
	};
	Segment(DrawParts[0], Top, Nock, 0.0035f);
	Segment(DrawParts[1], Bottom, Nock, 0.0035f);
	const FVector Grip = B.GetLocation();
	const FVector Dir = (Grip - Nock).GetSafeNormal();
	const FVector Tip = Grip + Dir * 14.f;
	Segment(DrawParts[2], Nock, Tip, 0.008f);
	DrawParts[3]->SetWorldLocationAndRotation(Tip + Dir * 3.f, FRotationMatrix::MakeFromZ(Dir).Rotator());
	DrawParts[3]->SetWorldScale3D(FVector(0.025f, 0.025f, 0.06f));
}

void ARPGPlayerCharacter::UpdatePoses(float Dt)
{
	CastKick -= Dt; CastHold -= Dt; BowRelease -= Dt;
	if (!bDrawing) BowOut -= Dt; else BowOut = 1.2f;
	OrbFlash = FMath::Max(0.f, OrbFlash - Dt * 4.f);
	if (bAttacking) BowOut = 0.f;   // swinging the dagger puts the bow away

	TArray<FName>& P = PoseMesh->ActivePoses;
	P.Reset();
	if (bDead) return;
	if (GuardStyle().IsValid()) P.Add(TEXT("guard"));
	if (CastKick > 0.f) P.Add(TEXT("cast_kick"));
	else if (CastHold > 0.f) P.Add(TEXT("cast"));
	const bool bBowOut = bDrawing || BowRelease > 0.f || BowOut > 0.f;
	if (bBowOut)
	{
		P.Add(TEXT("bow_aim"));
		if (bDrawing) P.Add(TEXT("bow_draw"));
		else if (BowRelease > 0.f) P.Add(TEXT("bow_release"));
	}
	// Thief: bow in hand + dagger on the belt while shooting; dagger in hand + bow on the back otherwise.
	SetKitHolstered(TEXT("bow"), !bBowOut);
	SetKitHolstered(TEXT("dagger"), bBowOut && TSJson::Str(TSJson::Obj(Style(), TEXT("secondary")), TEXT("type")) == TEXT("bow"));
	SetKitGlow(TEXT("staff"), OrbFlash);
}

void ARPGPlayerCharacter::UpdateArcPreview()
{
	// While drawing the bow: dots along the arc the arrow will fly, fading in with the draw.
	constexpr int32 N = 14;
	const bool bShow = bDrawing && !bDead;
	if (bShow && ArcDots.Num() < N)
	{
		while (ArcDots.Num() < N)
		{
			UStaticMeshComponent* Dot = NewObject<UStaticMeshComponent>(this);
			Dot->SetStaticMesh(TSAssets::Shape(TEXT("Sphere")));
			Dot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Dot->SetCastShadow(false);
			Dot->SetMaterial(0, AimMat);
			Dot->SetUsingAbsoluteLocation(true);
			Dot->SetUsingAbsoluteScale(true);
			Dot->SetupAttachment(RootComponent);
			Dot->RegisterComponent();
			ArcDots.Add(Dot);
		}
	}
	for (UStaticMeshComponent* Dot : ArcDots) Dot->SetVisibility(bShow);
	if (!bShow) return;

	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj B = TSJson::Obj(Style(), TEXT("secondary"));
	const float K = DrawFraction();
	const FVector From = Muzzle();
	const FVector To = ArrowTarget(From, D.Px(TSJson::Num(B, TEXT("range"), 520)) * (0.5f + 0.5f * K));
	FVector V;
	float G = 0.f, T = 1.f;
	ATSProjectile::ArcLaunch(this, From, To, D.Px(TSJson::Num(B, TEXT("speed"), 560)) * (0.6f + 0.4f * K), V, G, T);
	for (int32 I = 0; I < ArcDots.Num(); ++I)
	{
		const float t = T * float(I + 1) / float(ArcDots.Num());
		ArcDots[I]->SetWorldLocation(From + V * t - FVector(0, 0, 0.5f * G * t * t));
		ArcDots[I]->SetWorldScale3D(FVector(I + 1 == ArcDots.Num() ? 0.16f : 0.09f));
	}
	AimMat->SetScalarParameterValue(TEXT("Opacity"), 0.2f + 0.6f * K);
}

void ARPGPlayerCharacter::FaceThreat()
{
	// Blocking: face the nearest foe that's after you (chasing, winding up or recovering), so the shield covers the
	// danger even when the cursor is elsewhere. Nobody close: face the cursor as usual.
	const ATSCharacter* Best = TSPerception::NearestHunter(this, 1400.f);
	if (!Best) { FaceAim(); return; }
	const FVector To = Best->GetActorLocation() - GetActorLocation();
	if (To.SizeSquared2D() > 1.f) SetActorRotation(FRotator(0, To.Rotation().Yaw, 0));
}

void ARPGPlayerCharacter::FaceAim()
{
	if (bTopDown)
	{
		const FVector To = AimPoint() - GetActorLocation();
		if (To.SizeSquared2D() > 1.f) SetActorRotation(FRotator(0, To.Rotation().Yaw, 0));
		return;
	}
	if (Controller) SetActorRotation(FRotator(0, Controller->GetControlRotation().Yaw, 0));
}

ARPGCharacterBase* ARPGPlayerCharacter::TalkTarget() const
{
	const float Range = UTSData::Get(this).Px(UTSData::Get(this).Tuning(TEXT("interactRange"), 48)) + 60.f;
	ARPGCharacterBase* Best = nullptr;
	float BestD = Range;
	for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It)
	{
		ARPGCharacterBase* C = *It;
		if (C == this || C->IsDead() || C->DialogueRoot.IsEmpty() || C->IsLeaving()) continue;
		if (C->Team == ETSTeam::Hostile && !C->IsPassive()) continue;
		if (!C->CanBeTargeted(this)) continue;
		const float D = FVector::Dist2D(C->GetActorLocation(), GetActorLocation()) - C->Radius();
		if (D < BestD) { BestD = D; Best = C; }
	}
	return Best;
}

// ---------------------------------------------------------------------------------------------
// Primary: melee combo (template montage) or arcane bolt
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::OnAttack()
{
	if (Control->bInputLocked) return;
	// Top-down: LMB on the ground walks, on a foe fights, on a villager talks; in talk mode (E) it talks to
	// whoever it's on; while choosing an ability it casts. Shift+LMB attacks in place.
	// In action mode (E) a click on a lock picks it and a click on someone steals (crouched Thief); a plain click only
	// tries the lock and talks.
	bActionClick = bUseByKey = Control->IsTalkMode();
	if (Control->HandlePrimaryPress()) return;
	PrimaryAttack();
}

void ARPGPlayerCharacter::PrimaryAttack()
{
	Control->bAttackHeld = true;
	LastAttackInput = GetWorld()->GetTimeSeconds();
	if (bDead || IsDodging() || Tags.Has(TEXT("Staggered"))) return;
	TSPerception::Reveal(this);
	const TSJson::FObj Primary = TSJson::Obj(Style(), TEXT("primary"));
	if (TSJson::Str(Primary, TEXT("type")) == TEXT("bolt")) { if (BoltCooldown <= 0.f) FireBolt(); return; }
	if (!bAttacking) StartCombo();
}

void ARPGPlayerCharacter::OnAttackReleased() { Control->OnPrimaryReleased(); }

// ---------------------------------------------------------------------------------------------
// Click-to-move rules (the walking is UTSHeroControl's)
// ---------------------------------------------------------------------------------------------

const ATSCharacter* ARPGPlayerCharacter::ClickedFoe() const
{
	return Control->GetGoal() == ETSClickGoal::Attack ? Control->GoalTarget() : nullptr;
}

bool ARPGPlayerCharacter::InAttackRange(const ATSCharacter* T) const
{
	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj Primary = TSJson::Obj(Style(), TEXT("primary"));
	const float Dist = FVector::Dist2D(T->GetActorLocation(), GetActorLocation()) - T->Radius();
	if (TSJson::Str(Primary, TEXT("type")) == TEXT("bolt"))
	{
		if (Dist > D.Px(TSJson::Num(Primary, TEXT("range"), 440)) * 0.8f) return false;
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ClickLOS), false, this);
		Q.AddIgnoredActor(T);
		return !GetWorld()->LineTraceSingleByChannel(H, Chest(), T->Chest(), ECC_Visibility, Q);
	}
	const TArray<TSharedPtr<FJsonValue>> Combo = TSJson::Arr(Primary, TEXT("combo"));
	const double Reach = Combo.IsEmpty() ? 44 : TSJson::Num(Combo[0]->AsObject(), TEXT("range"), 44);
	return Dist <= (D.Px(Reach) + Radius()) * 0.85f;
}

void ARPGPlayerCharacter::StartCombo()
{
	const TArray<TSharedPtr<FJsonValue>> Combo = TSJson::Arr(TSJson::Obj(Style(), TEXT("primary")), TEXT("combo"));
	if (Combo.IsEmpty() || !ComboMontage) return;
	if (!Stats->Spend(RPGStat::Stamina, float(TSJson::Num(Combo[0]->AsObject(), TEXT("stamina"), 12))))
	{
		UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("No stamina"), FLinearColor(0.5f, 0.82f, 0.5f), 0.8f);
		return;
	}
	ComboStep = 0;
	bAttacking = true;
	bDrawing = false;
	FaceAim();
	// Heavier styles swing slower, daggers faster (until style-specific animations arrive).
	const float Rate = float(TSJson::Num(Style(), TEXT("animRate"), 1.0));
	PlayMontage(ComboMontage, Rate);
	SpriteAttackAt = GetWorld()->GetTimeSeconds();
	FOnMontageEnded Ended;
	Ended.BindUObject(this, &ARPGPlayerCharacter::OnComboEnded);
	Anim()->Montage_SetEndDelegate(Ended, ComboMontage);
}

void ARPGPlayerCharacter::CheckCombo()
{
	if (!bAttacking) return;
	const TArray<TSharedPtr<FJsonValue>> Combo = TSJson::Arr(TSJson::Obj(Style(), TEXT("primary")), TEXT("combo"));
	const bool bBuffered = Control->bAttackHeld || GetWorld()->GetTimeSeconds() - LastAttackInput <= float(TSJson::Num(Style(), TEXT("comboWindow"), 0.45)) + 0.2f;
	const int32 Next = ComboStep + 1;
	if (!bBuffered || Next >= Combo.Num() || ComboSections.IsEmpty()) return;
	if (!Stats->Spend(RPGStat::Stamina, float(TSJson::Num(Combo[Next]->AsObject(), TEXT("stamina"), 12)))) return;
	ComboStep = Next;
	FaceAim();
	Anim()->Montage_JumpToSection(ComboSections[ComboStep % ComboSections.Num()], ComboMontage);
	SpriteAttackAt = GetWorld()->GetTimeSeconds();
}

void ARPGPlayerCharacter::DoAttackTrace(FName SourceBone)
{
	if (!bAttacking) return;
	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj St = Style();
	const TSJson::FObj Primary = TSJson::Obj(St, TEXT("primary"));
	const TArray<TSharedPtr<FJsonValue>> Combo = TSJson::Arr(Primary, TEXT("combo"));
	if (!Combo.IsValidIndex(ComboStep)) return;
	const TSJson::FObj A = Combo[ComboStep]->AsObject();

	const float Range = D.Px(TSJson::Num(A, TEXT("range"), 44)) + Radius();
	const float Arc = float(TSJson::Num(A, TEXT("arc"), 110));
	const float Base = Stats->Get(TEXT("weaponDamage")) * float(TSJson::Num(A, TEXT("mult"), 1)) * float(TSJson::Num(St, TEXT("damageMul"), 1));
	if (const double Lunge = TSJson::Num(A, TEXT("lunge"), 0)) Knock(Facing() * D.Px(Lunge));

	const TSJson::FObj Poison = [&]() -> TSJson::FObj { for (const FTSEffect& E : Stats->Effects) if (E.OnHit) return E.OnHit; return nullptr; }();
	for (ATSCharacter* E : TSCombat::Opponents(this))
	{
		const FVector To = E->GetActorLocation() - GetActorLocation();
		if (To.Size2D() - E->Radius() > Range || FMath::Abs(To.Z) > 200.f) continue;
		if (FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Facing(), To.GetSafeNormal2D()))) > Arc * 0.5f) continue;

		float Mul = 1.f;
		const double Backstab = TSJson::Num(St, TEXT("backstab"), 0);
		if (Backstab > 0 && FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(E->Facing(), (-To).GetSafeNormal2D()))) > 110.f)
		{
			Mul = float(Backstab);
			UTSFeedback::Get(this)->Float(E->Head() + FVector(0, 0, 45), TEXT("BACKSTAB"), FLinearColor(0.25f, 0.76f, 0.56f), 0.9f);
		}
		FTSHit H;
		H.Base = Base * Mul;
		H.Scaling = FName(TSJson::Str(Primary, TEXT("scaling"), TEXT("might")));
		H.Poise = float(TSJson::Num(A, TEXT("poise"), 10));
		H.Knockback = float(TSJson::Num(A, TEXT("knockback"), 100));
		if (TSCombat::Deal(this, E, H) && !E->IsDead() && Poison)
		{
			FTSEffect P;
			P.Id = TEXT("poison"); P.Name = TEXT("Poison");
			P.Duration = float(TSJson::Num(Poison, TEXT("duration"), 3));
			P.Period = float(TSJson::Num(Poison, TEXT("period"), 0.5));
			P.Dot = float(TSJson::Num(Poison, TEXT("damage"), 4)) * Stats->ScaleBy(FName(TSJson::Str(Poison, TEXT("scaling"))));
			P.Source = this;
			E->Stats->AddEffect(P);
		}
	}
}

void ARPGPlayerCharacter::OnComboEnded(UAnimMontage* Montage, bool bInterrupted)
{
	bAttacking = false;
}

void ARPGPlayerCharacter::FireBolt()
{
	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj P = TSJson::Obj(Style(), TEXT("primary"));
	BoltCooldown = float(TSJson::Num(P, TEXT("cooldown"), 0.45));
	CastSlow = 0.15f;
	FaceAim();
	PlayCast();
	const FVector From = Muzzle();
	const FVector Dir = AimDirection(From);
	FTSHit H;
	H.Base = float(TSJson::Num(P, TEXT("damage"), 5)) + Stats->Get(TEXT("weaponDamage")) * float(TSJson::Num(P, TEXT("weaponRatio"), 0.5));
	H.Scaling = FName(TSJson::Str(P, TEXT("scaling"), TEXT("focus")));
	H.Poise = float(TSJson::Num(P, TEXT("poise"), 10));
	H.Knockback = float(TSJson::Num(P, TEXT("knockback"), 60));
	ATSProjectile::Fire(this, From + Dir * 12.f, Dir, D.Px(TSJson::Num(P, TEXT("speed"), 480)), D.Px(TSJson::Num(P, TEXT("range"), 440)),
		D.Px(TSJson::Num(P, TEXT("radius"), 5)), TSJson::Color(TSJson::Str(P, TEXT("color"), TEXT("#b9a4ff"))), false, H);
}

// ---------------------------------------------------------------------------------------------
// Secondary: block or bow
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::OnSecondary()
{
	if (Control->bInputLocked) return;
	// RMB cancels the ability picker or talk mode before it blocks / draws.
	if (Control->CancelModes()) return;
	if (bDead) return;
	const FString Type = TSJson::Str(TSJson::Obj(Style(), TEXT("secondary")), TEXT("type"));
	if (Type == TEXT("block")) { bGuardHeld = true; GuardTime = 0.f; }
	else if (Type == TEXT("bow")) { bDrawing = true; DrawTime = 0.f; }
}

void ARPGPlayerCharacter::OnSecondaryReleased()
{
	bGuardHeld = false;
	if (bDrawing && !bAttacking && !IsDodging() && !Tags.Has(TEXT("Staggered"))) FireArrow(DrawTime);
	bDrawing = false;
}

bool ARPGPlayerCharacter::MeetsRequirement(const FString& Requirement, FString& Why) const
{
	if (Requirement == TEXT("shield") && TSJson::Str(TSJson::Obj(Style(), TEXT("secondary")), TEXT("type")) != TEXT("block"))
	{
		Why = TEXT("Needs a shield (press X)");
		return false;
	}
	return true;
}

void ARPGPlayerCharacter::OnParley(ATSCharacter* Target, const FString& Node)
{
	URPGSession::Get(this)->OpenDialogue(Target, Node);
}

TSJson::FObj ARPGPlayerCharacter::GuardStyle() const
{
	if (!bGuardHeld || bAttacking || IsDodging() || Tags.Has(TEXT("Staggered"))) return nullptr;
	const TSJson::FObj Sec = TSJson::Obj(Style(), TEXT("secondary"));
	return TSJson::Str(Sec, TEXT("type")) == TEXT("block") ? Sec : nullptr;
}


float ARPGPlayerCharacter::DrawFraction() const
{
	const float Full = float(TSJson::Num(TSJson::Obj(Style(), TEXT("secondary")), TEXT("drawTime"), 0.8));
	return bDrawing ? FMath::Clamp(DrawTime / Full, 0.f, 1.f) : 0.f;
}

void ARPGPlayerCharacter::FireArrow(float Held)
{
	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj B = TSJson::Obj(Style(), TEXT("secondary"));
	if (Held < TSJson::Num(B, TEXT("minDraw"), 0.12)) return;
	TSPerception::Reveal(this);
	const float K = FMath::Clamp(Held / float(TSJson::Num(B, TEXT("drawTime"), 0.8)), 0.f, 1.f);
	const bool bFull = K >= 1.f;
	const float MinMul = float(TSJson::Num(B, TEXT("minMul"), 0.35));
	const float Mul = (MinMul + (1.f - MinMul) * K) * (bFull ? float(TSJson::Num(B, TEXT("fullBonus"), 1.3)) : 1.f);
	FaceAim();
	const FVector From = Muzzle();
	const FVector Dir = AimDirection(From);
	PlayBowShot();
	FTSHit H;
	H.Base = (float(TSJson::Num(B, TEXT("damage"), 10)) + Stats->Get(TEXT("weaponDamage")) * float(TSJson::Num(B, TEXT("weaponRatio"), 0.5))) * Mul;
	H.Scaling = FName(TSJson::Str(B, TEXT("scaling"), TEXT("agility")));
	H.Poise = float(TSJson::Num(B, TEXT("poise"), 20)) * Mul;
	H.Knockback = float(TSJson::Num(B, TEXT("knockback"), 120)) * Mul;
	// A partial draw flies slower and not as far; the arrow arcs to land on its target.
	const float Range = D.Px(TSJson::Num(B, TEXT("range"), 520)) * (0.5f + 0.5f * K);
	ATSProjectile::FireArrow(this, From + Dir * 20.f, ArrowTarget(From, Range), D.Px(TSJson::Num(B, TEXT("speed"), 560)) * (0.6f + 0.4f * K),
		6.f, bFull ? FLinearColor(1.f, 0.85f, 0.3f) : TSJson::Color(TSJson::Str(B, TEXT("color"), TEXT("#e8d9b0"))), H);
	if (bFull) UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("Full draw!"), FLinearColor(1.f, 0.95f, 0.75f), 0.8f);
}

// ---------------------------------------------------------------------------------------------
// Dodge / dash
// ---------------------------------------------------------------------------------------------

bool ARPGPlayerCharacter::CanSneak() const { return TSJson::Obj(ClassDef, TEXT("sneak")).IsValid(); }

void ARPGPlayerCharacter::SetSneaking(bool bOn)
{
	if (bOn == IsSneaking() || (bOn && (!CanSneak() || bDead))) return;
	if (bOn) Tags.Add(TEXT("Sneaking"));
	else Tags.Remove(TEXT("Sneaking"));
	UpdateSpeed();
	UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), bOn ? TEXT("Sneaking") : TEXT("Standing"), FLinearColor(0.6f, 0.75f, 0.7f), 0.7f);
}

void ARPGPlayerCharacter::UpdateSpeed()
{
	const UTSData& D = UTSData::Get(this);
	const float Base = D.Px(TSJson::Num(ClassDef, TEXT("moveSpeed"), 165));
	GetCharacterMovement()->MaxWalkSpeed = Base * (IsSneaking() ? float(TSJson::Num(TSJson::Obj(ClassDef, TEXT("sneak")), TEXT("speed"), 0.5)) : 1.f);
}

void ARPGPlayerCharacter::OnDodge()
{
	if (Control->bInputLocked) return;
	// The Thief crouches on Space instead (quiet and slow; E steals while crouched).
	if (CanSneak()) { SetSneaking(!IsSneaking()); return; }
	if (IsDodging() || bDead || Tags.Has(TEXT("Staggered"))) return;
	const UTSData& D = UTSData::Get(this);
	const TSJson::FObj Base = TSJson::Obj(D.Section(TEXT("tuning")), TEXT("dodge"));
	const TSJson::FObj Over = TSJson::Obj(ClassDef, TEXT("dodge"));
	auto Get = [&](const TCHAR* K, double Def) { return TSJson::Num(Over, K, TSJson::Num(Base, K, Def)); };
	if (!Stats->Spend(RPGStat::Stamina, float(Get(TEXT("cost"), 22))))
	{
		UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("No stamina"), FLinearColor(0.5f, 0.82f, 0.5f), 0.8f);
		return;
	}
	const FRotator Yaw = MoveFrame();
	const FVector Wish = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * MoveInput.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * MoveInput.X;
	// No WASD: top-down rolls toward the cursor (or along a click-to-move path), third person forward.
	FVector Fallback = GetActorForwardVector();
	if (bTopDown)
	{
		FVector Corner;
		if (Control->NextCorner(Corner)) Fallback = (Corner - GetActorLocation()).GetSafeNormal2D();
		else if (const FVector To = (AimPoint() - GetActorLocation()).GetSafeNormal2D(); !To.IsNearlyZero()) Fallback = To;
	}
	const FVector Dir = Wish.IsNearlyZero() ? Fallback : Wish.GetSafeNormal();
	Tags.Add(TEXT("Invulnerable"), float(Get(TEXT("iframes"), 0.26)));
	StartDash(Dir, D.Px(Get(TEXT("speed"), 430)), float(Get(TEXT("duration"), 0.32)), nullptr);
	if (UAnimSequenceBase* Dash = TSAssets::Load<UAnimSequenceBase>(RPGAssets::DashAnim))
		if (UAnimInstance* A = Anim()) A->PlaySlotAnimationAsDynamicMontage(Dash, TEXT("DefaultSlot"), 0.05f, 0.15f, 1.25f);
}

void ARPGPlayerCharacter::StartDash(const FVector& Dir, float Speed, float Duration, const TSJson::FObj& Strike)
{
	if (UAnimInstance* A = Anim()) A->Montage_Stop(0.1f);
	bAttacking = false;
	bDrawing = false;
	DodgeDir = Dir.GetSafeNormal2D();
	DodgeSpeed = Speed;
	DodgeTime = Duration;
	DashStrike = Strike;
	DashHit.Reset();
	Tags.Add(TEXT("Dodging"), Duration);
	if (Strike) Tags.Add(TEXT("Invulnerable"), Duration + 0.05f);
	SetActorRotation(DodgeDir.Rotation());
}

// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::DrinkPotion()
{
	for (const FTSItem& It : Inventory->Items)
	{
		if (It.Id != TEXT("potion")) continue;
		if (!Inventory->Use(It.Uid)) UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("Already at full health"), FLinearColor(0.67f, 0.67f, 0.73f), 0.8f);
		return;
	}
	UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("No potions"), FLinearColor(0.67f, 0.67f, 0.73f), 0.8f);
}

void ARPGPlayerCharacter::SwapStyle()
{
	const int32 N = TSJson::Arr(ClassDef, TEXT("styles")).Num();
	if (N < 2 || bAttacking || IsDodging()) return;
	StyleIndex = (StyleIndex + 1) % N;
	ComboStep = 0;
	bGuardHeld = bDrawing = false;
	RefreshWeapons();
	UTSFeedback::Get(this)->Toast(TEXT("Switched to ") + TSJson::Str(Style(), TEXT("name")), NameColor);
}

USkinnedMeshComponent* ARPGPlayerCharacter::BodyMesh() const { return PoseMesh; }

void ARPGPlayerCharacter::RefreshWeapons()
{
	const TArray<TSharedPtr<FJsonValue>> Styles = TSJson::Arr(ClassDef, TEXT("styles"));
	if (Styles.IsEmpty()) return;
	TArray<FString> Kits;
	for (const TSharedPtr<FJsonValue>& V : TSJson::Arr(TSJson::Obj(UTSData::Get(this).World(), TEXT("styleKits")), Styles[FMath::Clamp(StyleIndex, 0, Styles.Num() - 1)]->AsString())) Kits.Add(V->AsString());
	SetWeaponKits(Kits);
	BuildOrbLight();
}

void ARPGPlayerCharacter::BuildOrbLight()
{
	if (OrbLight) { OrbLight->DestroyComponent(); OrbLight = nullptr; }
	OrbCandelas = OrbSight = 0.f;
	if (!NightGlow) return;   // (not begun play yet: BeginPlay builds it)
	const TSJson::FObj AllKits = TSJson::Obj(UTSData::Get(this).World(), TEXT("kits"));
	const TArray<TSharedPtr<FJsonValue>> Styles = TSJson::Arr(ClassDef, TEXT("styles"));
	if (Styles.IsEmpty()) return;
	for (const TSharedPtr<FJsonValue>& K : TSJson::Arr(TSJson::Obj(UTSData::Get(this).World(), TEXT("styleKits")), Styles[FMath::Clamp(StyleIndex, 0, Styles.Num() - 1)]->AsString()))
	{
		for (const TSharedPtr<FJsonValue>& P : TSJson::Arr(TSJson::Obj(AllKits, K->AsString()), TEXT("parts")))
		{
			const TSJson::FObj NL = TSJson::Obj(P->AsObject(), TEXT("nightLight"));
			UStaticMeshComponent* Orb = NL ? KitGlow(K->AsString()) : nullptr;
			if (!Orb) continue;
			OrbLight = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("OrbLight")));
			OrbLight->SetupAttachment(Orb);
			OrbLight->SetIntensityUnits(ELightUnits::Candelas);
			OrbLight->SetAttenuationRadius(float(TSJson::Num(NL, TEXT("radius"), 900)));
			OrbLight->SetLightColor(TSJson::Color(TSJson::Str(NL, TEXT("color"), TEXT("#dcecff"))));
			OrbLight->SetCastShadows(false);
			OrbLight->RegisterComponent();
			OrbCandelas = float(TSJson::Num(NL, TEXT("candelas"), 12));
			OrbSight = float(TSJson::Num(NL, TEXT("sight"), 0));
			break;
		}
		if (OrbLight) break;
	}
	OnNightLevel(ATSSky::Night());   // start at the current level, then follow the events
}

void ARPGPlayerCharacter::OnNightLevel(float Night)
{
	const float K = FMath::SmoothStep(0.f, 1.f, Night);   // nothing by day, rising through dusk, full at night
	if (NightGlow) NightGlow->SetIntensity(5.f * Night);
	if (OrbLight) OrbLight->SetIntensity(OrbCandelas * K);
	ATSSky::SetCarriedLight(OrbSight * K);   // the orb lets the hero see further in the dark
}

void ARPGPlayerCharacter::Restore()
{
	Stats->Fill();
	ATSFX::Ring(GetWorld(), GetActorLocation() - FVector(0, 0, 86), 120.f, FLinearColor(0.37f, 0.88f, 0.54f), 0.5f);
}

void ARPGPlayerCharacter::OnStaggered()
{
	bAttacking = false;
	bGuardHeld = false;
	bDrawing = false;
	if (UAnimInstance* A = Anim()) A->Montage_Stop(0.15f);
}

void ARPGPlayerCharacter::Die(AActor* Killer)
{
	if (bDead) return;
	Super::Die(Killer);
	Control->ClearGoal();
	Control->ClosePicker(false);
	Control->SetTalkMode(false);
	DeathTimer = 3.f;
	ShieldBubble->SetVisibility(false);
	GuardArc->SetVisibility(false);
	AimLine->SetVisibility(false);
}

void ARPGPlayerCharacter::Respawn()
{
	URPGSession* Session = URPGSession::Get(this);
	const int32 Lost = FMath::FloorToInt(Inventory->Currency * UTSData::Get(this).Tuning(TEXT("deathGoldPenalty"), 0.1));
	Inventory->Currency -= Lost;
	bDead = false;
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	SetActorLocation(SpawnPoint, false, nullptr, ETeleportType::TeleportPhysics);
	Tags.Clear();
	UpdateSpeed();   // (no longer crouched)
	Stats->Effects.Reset();
	Stats->Fill();
	KnockVelocity = FVector::ZeroVector;
	for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (!It->IsDead() && !It->IsLeaving()) It->ResetToHome();
	if (Session->Duel.IsValid()) { Session->Duel = nullptr; Session->Story()->SetFlag(TEXT("duel_lost")); UTSFeedback::Get(Session)->Toast(TEXT("You lost the duel."), FLinearColor(1.f, 0.5f, 0.5f)); }
	UTSFeedback::Get(Session)->Toast(Lost ? FString::Printf(TEXT("You lost %d gold."), Lost) : FString(TEXT("You wake in the village.")), FLinearColor(1.f, 0.5f, 0.5f));
}

// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::UpdateVisuals(float Dt)
{
	// The Mage's barrier (held RMB): a bubble engulfs him, fading as his stamina runs down.
	const TSJson::FObj Guarding = GuardStyle();
	const bool bShield = !bDead && Guarding && TSJson::Bool(Guarding, TEXT("barrier"));
	ShieldBubble->SetVisibility(bShield);
	if (bShield)
	{
		// A column standing on the ground around the hero, as wide as the barrier's keep-out circle (nothing gets
		// inside it) and a little taller than the drawn sprite (or the body in the 3D look).
		const float R = UTSData::Get(this).Px(TSJson::Num(Guarding, TEXT("keepOut"), 40));
		const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const float Height = (Sprite ? Sprite->GetComponentScale().Y * 100.f : Half * 2.f) * 1.15f;
		ShieldBubble->SetWorldLocation(GetActorLocation() + FVector(0, 0, Height * 0.5f - Half));
		ShieldBubble->SetWorldScale3D(FVector(R / 50.f, R / 50.f, Height / 100.f));
		BarrierPulse += Dt;
		BubbleMat->SetScalarParameterValue(TEXT("Opacity"), 0.35f + 0.35f * Stats->Pool(RPGStat::Stamina).Current / FMath::Max(1.f, Stats->Max(RPGStat::Stamina)) + 0.06f * FMath::Sin(BarrierPulse * 6.f));
		BubbleMat->SetScalarParameterValue(TEXT("Intensity"), 5.f);
	}

	// Raised guard: the left arm comes up into a guard pose (procedural, until real block
	// animations arrive) and the shield on the forearm rises with it; a brief white flash marks the perfect-block window.
	const TSJson::FObj Guard = GuardStyle();
	const float Perfect = Guard ? float(TSJson::Num(Guard, TEXT("perfectWindow"), 0)) : 0.f;
	const bool bPerfect = Perfect > 0.f && GuardTime <= Perfect;
	GuardArc->SetVisibility(bPerfect);
	if (bPerfect)
	{
		GuardMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(3.f, 3.f, 3.f));
		GuardMat->SetScalarParameterValue(TEXT("Opacity"), 0.7f * (1.f - GuardTime / Perfect));
	}

	// Bow draw: an aim line that grows with the draw.
	AimLine->SetVisibility(false);   // (the straight aim line is replaced by the dotted arc below)
	UpdateArcPreview();
	if (bDrawing)
	{
		const float K = DrawFraction();
		const float Len = 150.f + 550.f * K;
		const FVector Dir = AimDirection();
		AimLine->SetWorldLocationAndRotation(Muzzle() + Dir * (20.f + Len * 0.5f), FRotationMatrix::MakeFromZ(Dir).Rotator());
		AimLine->SetWorldScale3D(FVector(0.02f, 0.02f, Len / 100.f));
		AimMat->SetScalarParameterValue(TEXT("Opacity"), 0.15f + 0.55f * K);
	}
}

void ARPGPlayerCharacter::Tick(float Dt)
{
	Super::Tick(Dt);
	URPGSession* Session = URPGSession::Get(this);
	UpdateArea();



	if (bDead)
	{
		DeathTimer -= Dt;
		if (DeathTimer <= 0.f) Respawn();
		return;
	}

	Stats->Pool(RPGStat::Stamina).RegenMul = 1.f;
	Stats->TickStats(Dt, [this](float H) { TSCombat::Heal(this, H); }, [this](float D, AActor* Src) { TSCombat::Dot(this, D, Src); });
	Abilities->TickCooldowns(Dt);
	BoltCooldown -= Dt;
	CastSlow -= Dt;
	if (bGuardHeld) GuardTime += Dt;
	if (bDrawing) DrawTime += Dt;
	bSpriteHold = bDrawing;
	UpdateVisuals(Dt);
	UpdatePoses(Dt);

	// Dodge / dash movement (and strike everything you pass through).
	if (DodgeTime > 0.f)
	{
		DodgeTime -= Dt;
		GetCharacterMovement()->Velocity.X = DodgeDir.X * DodgeSpeed;
		GetCharacterMovement()->Velocity.Y = DodgeDir.Y * DodgeSpeed;
		if (DashStrike)
		{
			for (ATSCharacter* E : TSCombat::Opponents(this))
			{
				if (DashHit.Contains(E) || FVector::Dist2D(E->GetActorLocation(), GetActorLocation()) > Radius() + E->Radius() + 40.f) continue;
				DashHit.Add(E);
				FTSHit H;
				H.Base = float(TSJson::Num(DashStrike, TEXT("damage"), 16));
				H.Scaling = FName(TSJson::Str(DashStrike, TEXT("scaling")));
				H.Poise = float(TSJson::Num(DashStrike, TEXT("poise"), 30));
				H.Knockback = float(TSJson::Num(DashStrike, TEXT("knockback"), 100));
				H.Dir = DodgeDir;
				TSCombat::Deal(this, E, H);
			}
		}
		return;
	}
	if (Tags.Has(TEXT("Staggered"))) return;

	// Hold LMB to keep attacking (bolt fires on cooldown; melee restarts the combo). A click-to-attack
	// goal does its own attacking once in range.
	if (Control->bAttackHeld && !bAttacking && Control->GetGoal() == ETSClickGoal::None)
	{
		const TSJson::FObj Primary = TSJson::Obj(Style(), TEXT("primary"));
		if (TSJson::Str(Primary, TEXT("type")) == TEXT("bolt")) { if (BoltCooldown <= 0.f) FireBolt(); }
		else StartCombo();
	}

	// Strafe toward the camera while blocking / drawing / casting; otherwise turn toward movement.
	const TSJson::FObj Sec = TSJson::Obj(Style(), TEXT("secondary"));
	const bool bAiming = GuardStyle().IsValid() || bDrawing || CastSlow > 0.f;
	GetCharacterMovement()->bOrientRotationToMovement = !bAiming && !bAttacking;
	if (bAiming && GuardStyle().IsValid() && !bDrawing) FaceThreat();   // the shield turns toward who's coming at you
	else if (bAiming) FaceAim();
	if (const TSJson::FObj G = GuardStyle())
	{
		// Holding a shield (or the Mage's barrier) wears you out: stamina drains, and when it's gone the guard drops.
		Stats->Pool(RPGStat::Stamina).RegenMul = float(TSJson::Num(G, TEXT("staminaRegenMul"), 0.35));
		const float Drain = float(TSJson::Num(G, TEXT("staminaDrain"), 0)) * Dt;
		if (Drain > 0.f)
		{
			Stats->Pool(RPGStat::Stamina).Current = FMath::Max(0.f, Stats->Pool(RPGStat::Stamina).Current - Drain);
			Stats->Pool(RPGStat::Stamina).Delay = FMath::Max(Stats->Pool(RPGStat::Stamina).Delay, 0.3f);
			if (Stats->Pool(RPGStat::Stamina).Current <= 0.f)
			{
				bGuardHeld = false;
				UTSFeedback::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("Too tired to block"), FLinearColor(0.5f, 0.82f, 0.5f), 0.8f);
			}
		}
	}

	// Movement: WASD / stick (cancels any click-to-move), or the click-to-move path.
	if (!MoveInput.IsNearlyZero()) Control->ClearGoal();
	const FVector ClickDir = Control->Update(Dt);
	if (Controller && (!MoveInput.IsNearlyZero() || !ClickDir.IsNearlyZero()))
	{
		float Mul = 1.f;
		if (bAttacking) Mul = float(TSJson::Num(UTSData::Get(this).Section(TEXT("combat")), TEXT("attackMoveMul"), 0.35));
		else if (bAiming && (GuardStyle().IsValid() || bDrawing)) Mul = float(TSJson::Num(Sec, TEXT("moveMul"), 0.5));
		else if (CastSlow > 0.f) Mul = 0.6f;
		if (!ClickDir.IsNearlyZero()) AddMovementInput(ClickDir, Mul);
		else
		{
			const FRotator Yaw = MoveFrame();
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), MoveInput.Y * Mul);
			AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), MoveInput.X * Mul);
		}
	}
}
