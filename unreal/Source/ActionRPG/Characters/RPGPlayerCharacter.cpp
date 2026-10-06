#include "RPGPlayerCharacter.h"
#include "ActionRPG.h"
#include "RPGData.h"
#include "RPGAssets.h"
#include "RPGCombat.h"
#include "RPGStory.h"
#include "RPGEnemy.h"
#include "RPGFX.h"
#include "RPGProjectile.h"
#include "RPGInventoryComponent.h"
#include "RPGAbilityComponent.h"
#include "RPGPoseMesh.h"
#include "RPGLook.h"
#include "RPGSprite.h"
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
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ARPGPlayerCharacter::ARPGPlayerCharacter()
{
	Team = ERPGTeam::Player;

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

	Inventory = CreateDefaultSubobject<URPGInventoryComponent>(TEXT("Inventory"));
	Abilities = CreateDefaultSubobject<URPGAbilityComponent>(TEXT("Abilities"));

	PoseMesh = CreateDefaultSubobject<URPGPoseMesh>(TEXT("PoseMesh"));
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
	ShieldBubble = MakeFX(TEXT("ShieldBubble"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	ShieldBubble->SetRelativeScale3D(FVector(1.5f, 1.5f, 2.1f));
	GuardArc = MakeFX(TEXT("GuardArc"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	GuardArc->SetRelativeLocationAndRotation(FVector(55, 0, 0), FRotator(90, 0, 0));
	GuardArc->SetRelativeScale3D(FVector(1.1f, 1.1f, 0.03f));
	AimLine = MakeFX(TEXT("AimLine"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ARPGPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj Cam = RPGJson::Obj(D.World3D(), TEXT("camera"));
	CameraBoom->TargetArmLength = float(RPGJson::Num(Cam, TEXT("armLength"), 430));
	CameraBoom->CameraLagSpeed = float(RPGJson::Num(Cam, TEXT("lagSpeed"), 12));
	const TArray<TSharedPtr<FJsonValue>> Off = RPGJson::Arr(Cam, TEXT("socketOffset"));
	if (Off.Num() == 3) CameraBoom->SocketOffset = FVector(Off[0]->AsNumber(), Off[1]->AsNumber(), Off[2]->AsNumber());
	Camera->FieldOfView = float(RPGJson::Num(Cam, TEXT("fov"), 80));

	// Top-down: a fixed 3/4 view that follows the hero but never turns, so the screen matches the map.
	bTopDown = IsTopDown(this);
	if (bTopDown)
	{
		const RPGJson::FObj Td = RPGJson::Obj(Cam, TEXT("topdown"));
		CameraBoom->bUsePawnControlRotation = false;
		CameraBoom->SetUsingAbsoluteRotation(true);
		CameraBoom->SetWorldRotation(FRotator(float(RPGJson::Num(Td, TEXT("pitch"), -55)), float(RPGJson::Num(Td, TEXT("yaw"), -90)), 0.f));
		CameraBoom->bDoCollisionTest = false;   // walls and roofs get cut away instead (ARPGWorldBuilder)
		CameraBoom->SocketOffset = FVector::ZeroVector;
		CameraBoom->TargetArmLength = ZoomTarget = float(RPGJson::Num(Td, TEXT("armLength"), 2000));
		CameraBoom->CameraLagSpeed = float(RPGJson::Num(Td, TEXT("lagSpeed"), 10));
		MinArm = float(RPGJson::Num(Td, TEXT("minArm"), 1100));
		MaxArm = float(RPGJson::Num(Td, TEXT("maxArm"), 3000));
		ZoomStep = float(RPGJson::Num(Td, TEXT("zoomStep"), 220));
		Camera->FieldOfView = float(RPGJson::Num(Td, TEXT("fov"), 50));

		// 2D look tests: HD-2D frames lower and tighter; Flat 2D looks straight down through an orthographic lens.
		const RPGJson::FObj L2 = RPGJson::Obj(D.World3D(), TEXT("looks2d"));
		CameraBoom->SetWorldRotation(RPGLook::CameraRotation());
		if (RPGLook::Mode() == RPGLook::EMode::HD2D)
		{
			const RPGJson::FObj H = RPGJson::Obj(L2, TEXT("hd2dCamera"));
			CameraBoom->TargetArmLength = ZoomTarget = float(RPGJson::Num(H, TEXT("armLength"), 3000));
			MinArm = float(RPGJson::Num(H, TEXT("minArm"), 2400));
			MaxArm = float(RPGJson::Num(H, TEXT("maxArm"), 3800));
			Camera->FieldOfView = float(RPGJson::Num(H, TEXT("fov"), 30));
			// Tilt-shift: focus on the hero, so the top and bottom of the screen go soft (a big virtual sensor makes
			// the depth of field shallow enough to show at this distance).
			FPostProcessSettings& PP = Camera->PostProcessSettings;
			// (Off by default: the user preferred everything crisp. hd2dCamera.tiltShift = true brings it back.)
			PP.bOverride_DepthOfFieldFocalDistance = RPGJson::Bool(H, TEXT("tiltShift"), false) && !FParse::Param(FCommandLine::Get(), TEXT("RPGNoDOF"));
			PP.DepthOfFieldFocalDistance = CameraBoom->TargetArmLength;
			PP.bOverride_DepthOfFieldFstop = true;         PP.DepthOfFieldFstop = float(RPGJson::Num(H, TEXT("focusFstop"), 0.5));
			PP.bOverride_DepthOfFieldMinFstop = true;      PP.DepthOfFieldMinFstop = 0.f;
			PP.bOverride_DepthOfFieldSensorWidth = true;   PP.DepthOfFieldSensorWidth = float(RPGJson::Num(H, TEXT("sensorWidth"), 400));
			Camera->PostProcessBlendWeight = 1.f;
		}
		else if (RPGLook::Mode() == RPGLook::EMode::Flat2D)
		{
			const RPGJson::FObj Fc = RPGJson::Obj(L2, TEXT("flatCamera"));
			Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
			Camera->SetOrthoWidth(ZoomTarget = float(RPGJson::Num(Fc, TEXT("orthoWidth"), 3800)));
			MinArm = float(RPGJson::Num(Fc, TEXT("minWidth"), 2800));
			MaxArm = float(RPGJson::Num(Fc, TEXT("maxWidth"), 5200));
			ZoomStep = 300.f;
			CameraBoom->TargetArmLength = 4000.f;
		}
	}

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

	ComboMontage = RPGAssets::Load<UAnimMontage>(RPGAssets::ComboMontage);
	if (ComboMontage) for (const FCompositeSection& S : ComboMontage->CompositeSections) ComboSections.Add(S.SectionName);

	UMaterialInterface* Fresnel = RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Fresnel.M_RPG_Fresnel"));
	BubbleMat = UMaterialInstanceDynamic::Create(Fresnel, this);
	BubbleMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.65f, 1.f));
	ShieldBubble->SetMaterial(0, BubbleMat);
	GuardMat = UMaterialInstanceDynamic::Create(Fresnel, this);
	GuardArc->SetMaterial(0, GuardMat);
	AimMat = UMaterialInstanceDynamic::Create(RPGAssets::Load<UMaterialInterface>(TEXT("/Game/RPG/Materials/M_RPG_Telegraph.M_RPG_Telegraph")), this);
	AimMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.93f, 0.7f));
	AimLine->SetMaterial(0, AimMat);

	SpawnPoint = GetActorLocation();
	if (!ClassDef.IsValid()) ApplyClass(ClassId, Sex);
}

RPGJson::FObj ARPGPlayerCharacter::Style() const
{
	const TArray<TSharedPtr<FJsonValue>> Styles = RPGJson::Arr(ClassDef, TEXT("styles"));
	if (Styles.IsEmpty()) return nullptr;
	const FString Id = Styles[FMath::Clamp(StyleIndex, 0, Styles.Num() - 1)]->AsString();
	return URPGData::Get(this).Entry(TEXT("weaponStyles"), Id);
}

void ARPGPlayerCharacter::ApplyClass(const FString& InClassId, const FString& InSex)
{
	const URPGData& D = URPGData::Get(this);
	ClassId = InClassId;
	Sex = InSex;
	ClassDef = D.Entry(TEXT("classes"), ClassId);
	StyleIndex = 0;
	DisplayName = RPGJson::Str(ClassDef, TEXT("name"));
	NameColor = RPGJson::Color(RPGJson::Str(ClassDef, TEXT("color")));

	const RPGJson::FObj PlayerLook = RPGJson::Obj(RPGJson::Obj(D.World3D(), TEXT("looks")), TEXT("player"));
	SetLook(RPGJson::Str(PlayerLook, Sex, Sex == TEXT("female") ? TEXT("quinn") : TEXT("manny")), FString(), 1.f);
	UseSprite(ClassId + (Sex == TEXT("female") ? TEXT("_f") : TEXT("_m")));
	// Render the posed copy; the animated mesh keeps animating (and firing notifies) while hidden.
	PoseMesh->SetSkinnedAssetAndUpdate(GetMesh()->GetSkeletalMeshAsset());
	PoseMesh->Source = GetMesh();
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetMesh()->SetVisibility(false, false);

	// Stats, as the prototype's classBaseStats().
	const RPGJson::FObj PlayerDef = D.Section(TEXT("player"));
	Stats->Base.Reset();
	const RPGJson::FObj Attrs = RPGJson::Obj(ClassDef, TEXT("attributes"));
	if (Attrs.IsValid()) for (const auto& KV : Attrs->Values) Stats->Base.Add(FName(*KV.Key), float(KV.Value->AsNumber()));
	Stats->Base.Add(TEXT("hpFlat"), float(RPGJson::Num(ClassDef, TEXT("hpBase"), 60)));
	Stats->Base.Add(TEXT("manaFlat"), float(RPGJson::Num(ClassDef, TEXT("manaBase"), 30)));
	Stats->Base.Add(TEXT("manaRegen"), float(RPGJson::Num(ClassDef, TEXT("manaRegen"), D.Tuning(TEXT("manaRegen"), 2.5))));
	Stats->Base.Add(TEXT("level"), 1.f);
	Stats->Base.Add(TEXT("critPct"), float(RPGJson::Num(ClassDef, TEXT("critPct"), 5)));
	Stats->Base.Add(TEXT("weaponDamage"), float(RPGJson::Num(PlayerDef, TEXT("unarmedDamage"), 4)));
	Stats->Effects.Reset();
	MaxPoise = Poise = float(RPGJson::Num(PlayerDef, TEXT("poise"), 60));
	Xp = 0;
	AttrPoints = 0;

	// Starting gear.
	for (const FString& Slot : { TEXT("weapon"), TEXT("armor"), TEXT("trinket") }) Stats->RemoveModifiers(FName(Slot));
	Inventory->Items.Reset();
	Inventory->Equipment.Reset();
	Inventory->Capacity = int32(RPGJson::Num(PlayerDef, TEXT("inventorySize"), 16));
	Inventory->Gold = int32(RPGJson::Num(PlayerDef, TEXT("startGold"), 10));
	for (const TSharedPtr<FJsonValue>& V : RPGJson::Arr(ClassDef, TEXT("startItems")))
	{
		const RPGJson::FObj S = V->AsObject();
		FRPGItem It = URPGInventoryComponent::MakeItem(this, RPGJson::Str(S, TEXT("item")));
		It.Qty = int32(RPGJson::Num(S, TEXT("qty"), 1));
		Inventory->Add(It);
		if (RPGJson::Bool(S, TEXT("equip"))) Inventory->Equip(It.Uid);
	}
	Stats->Fill();

	TArray<FString> AbilityIds;
	for (const TSharedPtr<FJsonValue>& V : RPGJson::Arr(ClassDef, TEXT("abilities"))) AbilityIds.Add(V->AsString());
	Abilities->Setup(AbilityIds);

	GetCharacterMovement()->MaxWalkSpeed = D.Px(RPGJson::Num(ClassDef, TEXT("moveSpeed"), 165));
	RefreshWeapons();
	UE_LOG(LogRPG, Display, TEXT("Player is a %s %s: HP %.0f, stamina %.0f, mana %.0f, speed %.0f uu/s."),
		*Sex, *ClassId, Stats->MaxHP(), Stats->MaxStamina(), Stats->MaxMana(), GetCharacterMovement()->MaxWalkSpeed);
}

int32 ARPGPlayerCharacter::XpToNext(const UObject* Ctx, int32 InLevel)
{
	const URPGData& D = URPGData::Get(Ctx);
	return FMath::RoundToInt(D.Tuning(TEXT("xpBase"), 40) * FMath::Pow(D.Tuning(TEXT("xpGrowth"), 1.45), InLevel - 1));
}

void ARPGPlayerCharacter::GainXp(int32 Amount)
{
	if (Amount <= 0) return;
	URPGStory* Story = URPGStory::Get(this);
	Xp += Amount;
	Story->Float(Head() + FVector(0, 0, 50), FString::Printf(TEXT("+%d XP"), Amount), FLinearColor(0.7f, 0.55f, 1.f), 0.8f);
	while (Xp >= XpToNext(this, Level()))
	{
		Xp -= XpToNext(this, Level());
		Stats->Base.FindOrAdd(TEXT("level")) += 1.f;
		const int32 Points = int32(URPGData::Get(this).Tuning(TEXT("pointsPerLevel"), 3));
		AttrPoints += Points;
		Stats->Fill();
		ARPGFX::Ring(GetWorld(), GetActorLocation() - FVector(0, 0, 86), 220.f, FLinearColor(1.f, 0.83f, 0.3f), 0.8f);
		Story->Toast(FString::Printf(TEXT("Level %d! +%d attribute points — press C"), Level(), Points), FLinearColor(1.f, 0.83f, 0.3f));
		for (int32 I = 0; I < Abilities->Ids.Num(); ++I)
		{
			const RPGJson::FObj A = Abilities->Def(Abilities->Ids[I]);
			if (int32(RPGJson::Num(A, TEXT("unlockLevel"), 1)) == Level())
				Story->Toast(FString::Printf(TEXT("New ability: %s [%d]"), *RPGJson::Str(A, TEXT("name")), I + 1), RPGJson::Color(RPGJson::Str(A, TEXT("color"))));
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
	const bool bTD = IsTopDown(this);
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
	const bool bLocked = bInputLocked;
	TGuardValue<bool> NoCursor(bScripted, true);
	bInputLocked = false;
	if (Action == TEXT("Attack")) bDown ? OnAttack() : OnAttackReleased();
	else if (Action == TEXT("Secondary")) bDown ? OnSecondary() : OnSecondaryReleased();
	else if (Action == TEXT("Dodge")) { if (bDown) OnDodge(); }
	else if (Action == TEXT("Jump")) { bDown ? OnJump() : StopJumping(); }
	else if (bDown) OnKey(Action);
	bInputLocked = bLocked;
}

void ARPGPlayerCharacter::OnJump()
{
	// A plain jump: the combat anim blueprint plays jump / fall / land from CharacterMovement state.
	if (bInputLocked || bDead || bAttacking || IsDodging() || Tags.Has(TEXT("Staggered"))) return;
	Jump();
}

void ARPGPlayerCharacter::OnMove(const FInputActionValue& V) { MoveInput = bInputLocked ? FVector2D::ZeroVector : V.Get<FVector2D>(); }

void ARPGPlayerCharacter::OnLook(const FInputActionValue& V)
{
	if (bInputLocked || bTopDown) return;   // top-down: the mouse moves the cursor, not the camera
	const FVector2D L = V.Get<FVector2D>();
	AddControllerYawInput(L.X * 0.6f);
	AddControllerPitchInput(-L.Y * 0.6f);
}

void ARPGPlayerCharacter::OnZoom(const FInputActionValue& V)
{
	if (bInputLocked || !bTopDown || bDead) return;
	const float Wheel = V.Get<float>();
	// Shift + wheel: the ability picker. Plain wheel: zoom (within limits).
	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC && PC->IsInputKeyDown(EKeys::LeftShift))
	{
		if (Picker < 0) OpenPicker();
		else CyclePicker(Wheel > 0.f ? -1 : 1);
		return;
	}
	ZoomTarget = FMath::Clamp(ZoomTarget - Wheel * ZoomStep, MinArm, MaxArm);
}

// ---------------------------------------------------------------------------------------------
// Ability picker: Shift + wheel, slow motion while choosing, release Shift (or click) to cast
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::OpenPicker()
{
	int32 Start = INDEX_NONE;
	for (int32 I = 0; I < Abilities->Ids.Num() && Start == INDEX_NONE; ++I)
	{
		const int32 S = (LastPicked + I) % Abilities->Ids.Num();
		if (Abilities->Unlocked(Abilities->Ids[S])) Start = S;
	}
	if (Start == INDEX_NONE)
	{
		URPGStory::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("No abilities yet"), FLinearColor(0.8f, 0.8f, 0.8f), 0.8f);
		return;
	}
	Picker = Start;
	const RPGJson::FObj Cfg = RPGJson::Obj(RPGJson::Obj(URPGData::Get(this).World3D(), TEXT("camera")), TEXT("abilityPicker"));
	UGameplayStatics::SetGlobalTimeDilation(this, float(RPGJson::Num(Cfg, TEXT("timeScale"), 0.2)));
}

void ARPGPlayerCharacter::CyclePicker(int32 Step)
{
	const int32 N = Abilities->Ids.Num();
	for (int32 I = 1; I <= N; ++I)
	{
		const int32 S = ((Picker + Step * I) % N + N) % N;   // wraps, skipping locked slots
		if (Abilities->Unlocked(Abilities->Ids[S])) { Picker = S; return; }
	}
}

void ARPGPlayerCharacter::ClosePicker(bool bCast)
{
	if (Picker < 0) return;
	const int32 Slot = Picker;
	Picker = -1;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	if (!bCast) return;
	LastPicked = Slot;
	Abilities->TryActivate(Slot);
}

void ARPGPlayerCharacter::OnPickReleased() { if (!bInputLocked) ClosePicker(true); }

void ARPGPlayerCharacter::TestPicker(int32 Steps, bool bOpenOnly)
{
	if (Picker < 0) OpenPicker();
	for (int32 I = 0; I < FMath::Abs(Steps); ++I) CyclePicker(Steps > 0 ? 1 : -1);
	if (!bOpenOnly) ClosePicker(true);
}

// ---------------------------------------------------------------------------------------------
// Talk mode (E): the cursor talks instead of attacking
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::SetTalkMode(bool bOn)
{
	bTalkMode = bOn;   // (the cursor shows it: CursorIcon)
}

FName ARPGPlayerCharacter::CursorIcon() const
{
	FVector O, R;
	if (!CursorRay(O, R) || bDead) return NAME_None;
	bool bHostile = false;
	const ARPGCharacterBase* On = UnderCursor(bHostile);
	if (bTalkMode) return On && TalkBlocker(On).IsEmpty() ? FName(TEXT("talk")) : FName(TEXT("talk_off"));
	// The weapon this class attacks with right now.
	auto Weapon = [this]() -> FName
	{
		if (bDrawing || BowOut > 0.f) return TEXT("arrow");
		if (RPGJson::Str(RPGJson::Obj(Style(), TEXT("primary")), TEXT("type")) == TEXT("bolt")) return TEXT("wand");
		return ClassId == TEXT("knight") ? FName(TEXT("sword")) : FName(TEXT("dagger"));
	};
	if (bDrawing) return TEXT("arrow");
	if (On) return bHostile ? Weapon() : FName(TEXT("talk"));
	const APlayerController* PC = Cast<APlayerController>(Controller);
	if (PC && PC->IsInputKeyDown(EKeys::LeftShift) && Picker < 0) return Weapon();   // attack in place
	return TEXT("pointer");
}

float ARPGPlayerCharacter::TalkRange() const
{
	const URPGData& D = URPGData::Get(this);
	return D.Px(D.Tuning(TEXT("interactRange"), 48)) + 60.f;
}

FString ARPGPlayerCharacter::TalkBlocker(const ARPGCharacterBase* C) const
{
	if (!C || C->IsDead() || C->IsLeaving()) return TEXT("...");
	const ARPGEnemy* E = Cast<ARPGEnemy>(C);
	if (C->DialogueRoot.IsEmpty())
		return E && !RPGJson::Bool(E->Def, TEXT("intelligent")) ? TEXT("It can't be reasoned with.") : TEXT("They have nothing to say.");
	// Foes talk only while their faction is still neutral; mid-fight it takes Silver Words.
	if (C->Team == ERPGTeam::Enemy && !C->IsPassive()) return TEXT("They're past talking.");
	return FString();
}

void ARPGPlayerCharacter::TryTalk(ARPGCharacterBase* C)
{
	SetTalkMode(false);
	const FString Why = TalkBlocker(C);
	if (!Why.IsEmpty())
	{
		if (C) URPGStory::Get(this)->Float(C->Head() + FVector(0, 0, 40), Why, FLinearColor(0.85f, 0.85f, 0.8f), 0.9f);
		return;
	}
	Click(C, false, C->GetActorLocation());
}

bool ARPGPlayerCharacter::IsTopDown(const UObject* WorldContext)
{
	return RPGJson::Str(RPGJson::Obj(URPGData::Get(WorldContext).World3D(), TEXT("camera")), TEXT("mode")) == TEXT("topdown");
}

FRotator ARPGPlayerCharacter::MoveFrame() const
{
	if (bTopDown) return FRotator(0, CameraBoom->GetComponentRotation().Yaw, 0);
	return FRotator(0, Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw, 0);
}

bool ARPGPlayerCharacter::CursorRay(FVector& Origin, FVector& Dir) const
{
	const APlayerController* PC = Cast<APlayerController>(Controller);
	return bTopDown && !bInputLocked && !bScripted && PC && PC->DeprojectMousePositionToWorld(Origin, Dir);
}

void ARPGPlayerCharacter::OnKey(FName Key)
{
	if (bInputLocked) return;
	URPGStory* Story = URPGStory::Get(this);
	if (Key == TEXT("Interact"))
	{
		// Top-down: E on someone talks to them; otherwise it toggles talk mode for the next click.
		FVector O, R;
		if (CursorRay(O, R))
		{
			bool bHostile = false;
			if (ARPGCharacterBase* C = UnderCursor(bHostile)) TryTalk(C);
			else SetTalkMode(!bTalkMode);
			return;
		}
		if (ARPGCharacterBase* T = TalkTarget()) Story->OpenDialogue(T);
		return;
	}
	if (Key == TEXT("Potion")) { DrinkPotion(); return; }
	if (Key == TEXT("Swap")) { SwapStyle(); return; }
	if (Key == TEXT("Ability1")) { Abilities->TryActivate(0); return; }
	if (Key == TEXT("Ability2")) { Abilities->TryActivate(1); return; }
	if (Key == TEXT("Ability3")) { Abilities->TryActivate(2); return; }
	if (Key == TEXT("Ability4")) { Abilities->TryActivate(3); return; }
	if (Key == TEXT("Debug")) { Story->bDebug = !Story->bDebug; return; }
	if (Key == TEXT("CheatLevel")) { if (Story->bDebug) GainXp(XpToNext(this, Level()) - Xp); return; }
	if (Key == TEXT("CheatGold")) { if (Story->bDebug) { Inventory->Gold += 100; Inventory->OnChanged.Broadcast(); } return; }
	if (Key == TEXT("Escape") && Picker >= 0) { ClosePicker(false); return; }   // Esc backs out of what you're doing first
	if (Key == TEXT("Escape") && bTalkMode) { SetTalkMode(false); return; }
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
		if (Goal == EClickGoal::Attack && GoalActor.IsValid()) return GoalActor->Chest();   // the enemy you clicked
		const FVector C = Chest();
		FVector O, R;
		if (CursorRay(O, R) && R.Z < -0.01f) return O + R * ((C.Z - O.Z) / R.Z);
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
	if (Goal == EClickGoal::Attack && GoalActor.IsValid()) return (GoalActor->Chest() - From).GetSafeNormal();
	FVector CamPos = Camera->GetComponentLocation(), CamFwd = Camera->GetForwardVector();
	FVector RayO, RayDir;
	const bool bCursor = CursorRay(RayO, RayDir);
	if (bTopDown && !bCursor) { CamPos = Chest(); CamFwd = (AimPoint() - CamPos).GetSafeNormal2D(); }
	const ARPGCharacterBase* Best = nullptr;
	float BestDot = FMath::Cos(FMath::DegreesToRadians(12.f));
	float BestMiss = 0.f;
	for (ARPGCharacterBase* E : RPGCombat::Opponents(this))
	{
		if (FVector::Dist(E->Chest(), Chest()) > 3000.f) continue;
		if (bCursor)
		{
			const float Miss = FMath::PointDistToLine(E->Chest(), RayDir, RayO) - E->Radius();
			if (Miss > 70.f || (Best && Miss >= BestMiss)) continue;
			FHitResult H;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(AimAssist), false, this);
			Q.AddIgnoredActor(E);
			if (GetWorld()->LineTraceSingleByChannel(H, Chest(), E->Chest(), ECC_Visibility, Q)) continue;
			BestMiss = Miss;
			Best = E;
			continue;
		}
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
	const ARPGCharacterBase* Best = nullptr;
	float BestAlong = MaxRange;
	if (Goal == EClickGoal::Attack && GoalActor.IsValid()) { Best = GoalActor.Get(); BestAlong = FVector::Dist2D(From, Best->Chest()); }
	else
	{
		for (ARPGCharacterBase* E : RPGCombat::Opponents(this))
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

void ARPGPlayerCharacter::OnAbilityUsed(const RPGJson::FObj& Ability)
{
	const FString Type = RPGJson::Str(Ability, TEXT("type"));
	const bool bMage = RPGJson::Str(RPGJson::Obj(Style(), TEXT("primary")), TEXT("type")) == TEXT("bolt");
	const bool bBow = RPGJson::Str(RPGJson::Obj(Style(), TEXT("secondary")), TEXT("type")) == TEXT("bow");
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
			M->SetStaticMesh(RPGAssets::Shape(P[0]));
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
	SetKitHolstered(TEXT("dagger"), bBowOut && RPGJson::Str(RPGJson::Obj(Style(), TEXT("secondary")), TEXT("type")) == TEXT("bow"));
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
			Dot->SetStaticMesh(RPGAssets::Shape(TEXT("Sphere")));
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

	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj B = RPGJson::Obj(Style(), TEXT("secondary"));
	const float K = DrawFraction();
	const FVector From = Muzzle();
	const FVector To = ArrowTarget(From, D.Px(RPGJson::Num(B, TEXT("range"), 520)) * (0.5f + 0.5f * K));
	FVector V;
	float G = 0.f, T = 1.f;
	ARPGProjectile::ArcLaunch(this, From, To, D.Px(RPGJson::Num(B, TEXT("speed"), 560)) * (0.6f + 0.4f * K), V, G, T);
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
	const ARPGCharacterBase* Best = nullptr;
	float BestD = 1400.f;
	for (ARPGCharacterBase* C : RPGCombat::Opponents(this))
	{
		const ARPGEnemy* E = Cast<ARPGEnemy>(C);
		if (!E || E->IsPassive() || !(E->State == ERPGEnemyState::Chase || E->State == ERPGEnemyState::Windup || E->State == ERPGEnemyState::Recover)) continue;
		const float Dist = FVector::Dist2D(E->GetActorLocation(), GetActorLocation());
		if (Dist < BestD) { BestD = Dist; Best = E; }
	}
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
	const float Range = URPGData::Get(this).Px(URPGData::Get(this).Tuning(TEXT("interactRange"), 48)) + 60.f;
	ARPGCharacterBase* Best = nullptr;
	float BestD = Range;
	for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It)
	{
		ARPGCharacterBase* C = *It;
		if (C == this || C->IsDead() || C->DialogueRoot.IsEmpty() || C->IsLeaving()) continue;
		if (C->Team == ERPGTeam::Enemy && !C->IsPassive()) continue;
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
	if (bInputLocked) return;
	if (Picker >= 0) { ClosePicker(true); return; }   // click while choosing: cast it now
	// Top-down: LMB on the ground walks, on a foe fights, on a villager talks; in talk mode (E) it talks to
	// whoever it's on. Shift+LMB attacks in place.
	const APlayerController* PC = Cast<APlayerController>(Controller);
	FVector O, R;
	if (CursorRay(O, R) && !(PC && PC->IsInputKeyDown(EKeys::LeftShift)))
	{
		bool bHostile = false;
		ARPGCharacterBase* On = UnderCursor(bHostile);
		if (On && (bTalkMode || !bHostile)) { TryTalk(On); return; }
		SetTalkMode(false);
		FVector Ground = GetActorLocation();
		CursorGround(Ground);
		Click(On, bHostile, Ground);
		return;
	}
	PrimaryAttack();
}

void ARPGPlayerCharacter::PrimaryAttack()
{
	bAttackHeld = true;
	LastAttackInput = GetWorld()->GetTimeSeconds();
	if (bDead || IsDodging() || Tags.Has(TEXT("Staggered"))) return;
	Tags.Remove(TEXT("Hidden"));
	const RPGJson::FObj Primary = RPGJson::Obj(Style(), TEXT("primary"));
	if (RPGJson::Str(Primary, TEXT("type")) == TEXT("bolt")) { if (BoltCooldown <= 0.f) FireBolt(); return; }
	if (!bAttacking) StartCombo();
}

void ARPGPlayerCharacter::OnAttackReleased() { bAttackHeld = false; bMoveHeld = false; }

// ---------------------------------------------------------------------------------------------
// Click-to-move (top-down)
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::TestClick(const FVector& Point, ARPGCharacterBase* On)
{
	const bool bHostile = On && RPGCombat::Opponents(this).Contains(On);
	Click(On, bHostile, Point);
	bAttackHeld = bMoveHeld = false;   // a single click, not a hold
}

bool ARPGPlayerCharacter::CursorGround(FVector& Out) const
{
	FVector O, R;
	if (!CursorRay(O, R)) return false;
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(CursorGround), false, this);
	if (GetWorld()->LineTraceSingleByChannel(H, O, O + R * 30000.f, ECC_Visibility, Q)) { Out = H.ImpactPoint; return true; }
	if (R.Z >= -0.01f) return false;
	Out = O + R * ((GetActorLocation().Z - O.Z) / R.Z);
	return true;
}

ARPGCharacterBase* ARPGPlayerCharacter::UnderCursor(bool& bHostile) const
{
	bHostile = false;
	FVector O, R;
	if (!CursorRay(O, R)) return nullptr;
	const TArray<ARPGCharacterBase*> Foes = RPGCombat::Opponents(this);
	ARPGCharacterBase* Best = nullptr;
	float BestMiss = 0.f;
	for (TActorIterator<ARPGCharacterBase> It(GetWorld()); It; ++It)
	{
		ARPGCharacterBase* C = *It;
		if (C == this || C->IsDead() || C->IsLeaving()) continue;
		const bool bFoe = Foes.Contains(C);
		if (!bFoe && C->DialogueRoot.IsEmpty()) continue;
		// Generous: anywhere on the body, plus a little slack around it.
		float Miss = FMath::Min(FMath::PointDistToLine(C->Chest(), R, O), FMath::PointDistToLine(C->GetActorLocation() - FVector(0, 0, 40), R, O));
		if (RPGLook::IsSprite())
		{
			// A sprite is drawn on a card standing at the feet: test points up the card, where its body is drawn.
			const FVector Up = -FRotationMatrix(RPGLook::CardRotation()).GetUnitAxis(EAxis::Y);
			const FVector Feet = C->GetActorLocation() - FVector(0, 0, C->GetSimpleCollisionHalfHeight());
			for (const float H : { 30.f, 90.f, 150.f })
				Miss = FMath::Min(Miss, FMath::PointDistToLine(Feet + Up * H * C->GetActorScale3D().Z, R, O));
		}
		Miss -= C->Radius();
		if (Miss > 45.f || (Best && Miss >= BestMiss)) continue;
		Best = C;
		BestMiss = Miss;
		bHostile = bFoe;
	}
	return Best;
}

void ARPGPlayerCharacter::Click(ARPGCharacterBase* On, bool bHostile, const FVector& Ground)
{
	if (bDead) return;
	Path.Reset();
	RepathIn = 0.f;
	if (On)
	{
		Goal = bHostile ? EClickGoal::Attack : EClickGoal::Talk;
		GoalActor = On;
		GoalPoint = On->GetActorLocation();
		bAttackHeld = bHostile;   // held LMB keeps attacking once in range
		bMoveHeld = false;
		return;
	}
	Goal = EClickGoal::Move;
	GoalActor = nullptr;
	GoalPoint = Ground;
	bMoveHeld = true;
	bAttackHeld = false;
}

bool ARPGPlayerCharacter::ClickDestination(FVector& Out) const
{
	if (Goal != EClickGoal::Move) return false;
	Out = GoalPoint;
	return true;
}

void ARPGPlayerCharacter::Repath(const FVector& To)
{
	Path.Reset();
	PathIndex = 1;
	RepathIn = 0.3f;
	if (const UNavigationPath* P = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), GetActorLocation(), To, this))
		if (P->IsValid() && P->PathPoints.Num() >= 2) { Path = P->PathPoints; return; }
	Path = { GetActorLocation(), To };   // no navmesh (yet): straight line
}

bool ARPGPlayerCharacter::InAttackRange(const ARPGCharacterBase* T) const
{
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj Primary = RPGJson::Obj(Style(), TEXT("primary"));
	const float Dist = FVector::Dist2D(T->GetActorLocation(), GetActorLocation()) - T->Radius();
	if (RPGJson::Str(Primary, TEXT("type")) == TEXT("bolt"))
	{
		if (Dist > D.Px(RPGJson::Num(Primary, TEXT("range"), 440)) * 0.8f) return false;
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ClickLOS), false, this);
		Q.AddIgnoredActor(T);
		return !GetWorld()->LineTraceSingleByChannel(H, Chest(), T->Chest(), ECC_Visibility, Q);
	}
	const TArray<TSharedPtr<FJsonValue>> Combo = RPGJson::Arr(Primary, TEXT("combo"));
	const double Reach = Combo.IsEmpty() ? 44 : RPGJson::Num(Combo[0]->AsObject(), TEXT("range"), 44);
	return Dist <= (D.Px(Reach) + Radius()) * 0.85f;
}

FVector ARPGPlayerCharacter::UpdateClickGoal(float Dt)
{
	if (Goal == EClickGoal::None) return FVector::ZeroVector;
	ARPGCharacterBase* T = GoalActor.Get();
	if (Goal != EClickGoal::Move && (!T || T->IsDead() || T->IsLeaving())) { ClearGoal(); bAttackHeld = false; return FVector::ZeroVector; }

	if (Goal == EClickGoal::Attack)
	{
		if (InAttackRange(T))
		{
			// In range: swing / shoot. Holding LMB keeps the goal (and the attacks) going; a single click attacks once.
			Path.Reset();
			const bool bHeld = bAttackHeld;
			if (!bAttacking) PrimaryAttack();
			if (!bHeld) { bAttackHeld = false; ClearGoal(); }
			return FVector::ZeroVector;
		}
		if (bAttacking) return FVector::ZeroVector;   // finish the swing before chasing
	}
	if (Goal == EClickGoal::Talk && FVector::Dist2D(T->GetActorLocation(), GetActorLocation()) - T->Radius() <= TalkRange())
	{
		ClearGoal();
		if (TalkBlocker(T).IsEmpty()) URPGStory::Get(this)->OpenDialogue(T);   // (they may have turned hostile on the way)
		return FVector::ZeroVector;
	}

	// Holding LMB after a ground click: the destination follows the cursor.
	if (Goal == EClickGoal::Move && bMoveHeld) { FVector G; if (CursorGround(G)) GoalPoint = G; }
	if (T) GoalPoint = T->GetActorLocation();

	RepathIn -= Dt;
	if (Path.IsEmpty() || RepathIn <= 0.f) Repath(GoalPoint);
	while (PathIndex < Path.Num() && FVector::Dist2D(Path[PathIndex], GetActorLocation()) < 45.f) ++PathIndex;
	if (PathIndex >= Path.Num())
	{
		if (Goal == EClickGoal::Move && !bMoveHeld) ClearGoal();
		return FVector::ZeroVector;
	}
	return (Path[PathIndex] - GetActorLocation()).GetSafeNormal2D();
}

void ARPGPlayerCharacter::StartCombo()
{
	const TArray<TSharedPtr<FJsonValue>> Combo = RPGJson::Arr(RPGJson::Obj(Style(), TEXT("primary")), TEXT("combo"));
	if (Combo.IsEmpty() || !ComboMontage) return;
	if (!Stats->SpendStamina(float(RPGJson::Num(Combo[0]->AsObject(), TEXT("stamina"), 12))))
	{
		URPGStory::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("No stamina"), FLinearColor(0.5f, 0.82f, 0.5f), 0.8f);
		return;
	}
	ComboStep = 0;
	bAttacking = true;
	bDrawing = false;
	FaceAim();
	// Heavier styles swing slower, daggers faster (until style-specific animations arrive).
	const float Rate = float(RPGJson::Num(Style(), TEXT("animRate"), 1.0));
	PlayMontage(ComboMontage, Rate);
	SpriteAttackAt = GetWorld()->GetTimeSeconds();
	FOnMontageEnded Ended;
	Ended.BindUObject(this, &ARPGPlayerCharacter::OnComboEnded);
	Anim()->Montage_SetEndDelegate(Ended, ComboMontage);
}

void ARPGPlayerCharacter::CheckCombo()
{
	if (!bAttacking) return;
	const TArray<TSharedPtr<FJsonValue>> Combo = RPGJson::Arr(RPGJson::Obj(Style(), TEXT("primary")), TEXT("combo"));
	const bool bBuffered = bAttackHeld || GetWorld()->GetTimeSeconds() - LastAttackInput <= float(RPGJson::Num(Style(), TEXT("comboWindow"), 0.45)) + 0.2f;
	const int32 Next = ComboStep + 1;
	if (!bBuffered || Next >= Combo.Num() || ComboSections.IsEmpty()) return;
	if (!Stats->SpendStamina(float(RPGJson::Num(Combo[Next]->AsObject(), TEXT("stamina"), 12)))) return;
	ComboStep = Next;
	FaceAim();
	Anim()->Montage_JumpToSection(ComboSections[ComboStep % ComboSections.Num()], ComboMontage);
	SpriteAttackAt = GetWorld()->GetTimeSeconds();
}

void ARPGPlayerCharacter::DoAttackTrace(FName SourceBone)
{
	if (!bAttacking) return;
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj St = Style();
	const RPGJson::FObj Primary = RPGJson::Obj(St, TEXT("primary"));
	const TArray<TSharedPtr<FJsonValue>> Combo = RPGJson::Arr(Primary, TEXT("combo"));
	if (!Combo.IsValidIndex(ComboStep)) return;
	const RPGJson::FObj A = Combo[ComboStep]->AsObject();

	const float Range = D.Px(RPGJson::Num(A, TEXT("range"), 44)) + Radius();
	const float Arc = float(RPGJson::Num(A, TEXT("arc"), 110));
	const float Base = Stats->Get(TEXT("weaponDamage")) * float(RPGJson::Num(A, TEXT("mult"), 1)) * float(RPGJson::Num(St, TEXT("damageMul"), 1));
	if (const double Lunge = RPGJson::Num(A, TEXT("lunge"), 0)) Knock(Facing() * D.Px(Lunge));

	const RPGJson::FObj Poison = [&]() -> RPGJson::FObj { for (const FRPGEffect& E : Stats->Effects) if (E.OnHitPoison) return E.OnHitPoison; return nullptr; }();
	for (ARPGCharacterBase* E : RPGCombat::Opponents(this))
	{
		const FVector To = E->GetActorLocation() - GetActorLocation();
		if (To.Size2D() - E->Radius() > Range || FMath::Abs(To.Z) > 200.f) continue;
		if (FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Facing(), To.GetSafeNormal2D()))) > Arc * 0.5f) continue;

		float Mul = 1.f;
		const double Backstab = RPGJson::Num(St, TEXT("backstab"), 0);
		if (Backstab > 0 && FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(E->Facing(), (-To).GetSafeNormal2D()))) > 110.f)
		{
			Mul = float(Backstab);
			URPGStory::Get(this)->Float(E->Head() + FVector(0, 0, 45), TEXT("BACKSTAB"), FLinearColor(0.25f, 0.76f, 0.56f), 0.9f);
		}
		FRPGHit H;
		H.Base = Base * Mul;
		H.Scaling = FName(RPGJson::Str(Primary, TEXT("scaling"), TEXT("might")));
		H.Poise = float(RPGJson::Num(A, TEXT("poise"), 10));
		H.Knockback = float(RPGJson::Num(A, TEXT("knockback"), 100));
		if (RPGCombat::Deal(this, E, H) && !E->IsDead() && Poison)
		{
			FRPGEffect P;
			P.Id = TEXT("poison"); P.Name = TEXT("Poison");
			P.Duration = float(RPGJson::Num(Poison, TEXT("duration"), 3));
			P.Period = float(RPGJson::Num(Poison, TEXT("period"), 0.5));
			P.Dot = float(RPGJson::Num(Poison, TEXT("damage"), 4)) * RPGCombat::ScaleBy(Stats, FName(RPGJson::Str(Poison, TEXT("scaling"))));
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
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj P = RPGJson::Obj(Style(), TEXT("primary"));
	BoltCooldown = float(RPGJson::Num(P, TEXT("cooldown"), 0.45));
	CastSlow = 0.15f;
	FaceAim();
	PlayCast();
	const FVector From = Muzzle();
	const FVector Dir = AimDirection(From);
	FRPGHit H;
	H.Base = float(RPGJson::Num(P, TEXT("damage"), 5)) + Stats->Get(TEXT("weaponDamage")) * float(RPGJson::Num(P, TEXT("weaponRatio"), 0.5));
	H.Scaling = FName(RPGJson::Str(P, TEXT("scaling"), TEXT("focus")));
	H.Poise = float(RPGJson::Num(P, TEXT("poise"), 10));
	H.Knockback = float(RPGJson::Num(P, TEXT("knockback"), 60));
	ARPGProjectile::Fire(this, From + Dir * 12.f, Dir, D.Px(RPGJson::Num(P, TEXT("speed"), 480)), D.Px(RPGJson::Num(P, TEXT("range"), 440)),
		D.Px(RPGJson::Num(P, TEXT("radius"), 5)), RPGJson::Color(RPGJson::Str(P, TEXT("color"), TEXT("#b9a4ff"))), false, H);
}

// ---------------------------------------------------------------------------------------------
// Secondary: block or bow
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::OnSecondary()
{
	if (bInputLocked) return;
	// RMB cancels the ability picker or talk mode before it blocks / draws.
	if (Picker >= 0) { ClosePicker(false); return; }
	if (bTalkMode) { SetTalkMode(false); return; }
	if (bDead) return;
	const FString Type = RPGJson::Str(RPGJson::Obj(Style(), TEXT("secondary")), TEXT("type"));
	if (Type == TEXT("block")) { bGuardHeld = true; GuardTime = 0.f; }
	else if (Type == TEXT("bow")) { bDrawing = true; DrawTime = 0.f; }
}

void ARPGPlayerCharacter::OnSecondaryReleased()
{
	bGuardHeld = false;
	if (bDrawing && !bAttacking && !IsDodging() && !Tags.Has(TEXT("Staggered"))) FireArrow(DrawTime);
	bDrawing = false;
}

RPGJson::FObj ARPGPlayerCharacter::GuardStyle() const
{
	if (!bGuardHeld || bAttacking || IsDodging() || Tags.Has(TEXT("Staggered"))) return nullptr;
	const RPGJson::FObj Sec = RPGJson::Obj(Style(), TEXT("secondary"));
	return RPGJson::Str(Sec, TEXT("type")) == TEXT("block") ? Sec : nullptr;
}

RPGJson::FObj ARPGPlayerCharacter::PassiveStyle() const { return RPGJson::Obj(Style(), TEXT("passive")); }

float ARPGPlayerCharacter::DrawFraction() const
{
	const float Full = float(RPGJson::Num(RPGJson::Obj(Style(), TEXT("secondary")), TEXT("drawTime"), 0.8));
	return bDrawing ? FMath::Clamp(DrawTime / Full, 0.f, 1.f) : 0.f;
}

void ARPGPlayerCharacter::FireArrow(float Held)
{
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj B = RPGJson::Obj(Style(), TEXT("secondary"));
	if (Held < RPGJson::Num(B, TEXT("minDraw"), 0.12)) return;
	Tags.Remove(TEXT("Hidden"));
	const float K = FMath::Clamp(Held / float(RPGJson::Num(B, TEXT("drawTime"), 0.8)), 0.f, 1.f);
	const bool bFull = K >= 1.f;
	const float MinMul = float(RPGJson::Num(B, TEXT("minMul"), 0.35));
	const float Mul = (MinMul + (1.f - MinMul) * K) * (bFull ? float(RPGJson::Num(B, TEXT("fullBonus"), 1.3)) : 1.f);
	FaceAim();
	const FVector From = Muzzle();
	const FVector Dir = AimDirection(From);
	PlayBowShot();
	FRPGHit H;
	H.Base = (float(RPGJson::Num(B, TEXT("damage"), 10)) + Stats->Get(TEXT("weaponDamage")) * float(RPGJson::Num(B, TEXT("weaponRatio"), 0.5))) * Mul;
	H.Scaling = FName(RPGJson::Str(B, TEXT("scaling"), TEXT("agility")));
	H.Poise = float(RPGJson::Num(B, TEXT("poise"), 20)) * Mul;
	H.Knockback = float(RPGJson::Num(B, TEXT("knockback"), 120)) * Mul;
	// A partial draw flies slower and not as far; the arrow arcs to land on its target.
	const float Range = D.Px(RPGJson::Num(B, TEXT("range"), 520)) * (0.5f + 0.5f * K);
	ARPGProjectile::FireArrow(this, From + Dir * 20.f, ArrowTarget(From, Range), D.Px(RPGJson::Num(B, TEXT("speed"), 560)) * (0.6f + 0.4f * K),
		6.f, bFull ? FLinearColor(1.f, 0.85f, 0.3f) : RPGJson::Color(RPGJson::Str(B, TEXT("color"), TEXT("#e8d9b0"))), H);
	if (bFull) URPGStory::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("Full draw!"), FLinearColor(1.f, 0.95f, 0.75f), 0.8f);
}

// ---------------------------------------------------------------------------------------------
// Dodge / dash
// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::OnDodge()
{
	if (bInputLocked) return;
	if (IsDodging() || bDead || Tags.Has(TEXT("Staggered"))) return;
	const URPGData& D = URPGData::Get(this);
	const RPGJson::FObj Base = RPGJson::Obj(D.Section(TEXT("tuning")), TEXT("dodge"));
	const RPGJson::FObj Over = RPGJson::Obj(ClassDef, TEXT("dodge"));
	auto Get = [&](const TCHAR* K, double Def) { return RPGJson::Num(Over, K, RPGJson::Num(Base, K, Def)); };
	if (!Stats->SpendStamina(float(Get(TEXT("cost"), 22))))
	{
		URPGStory::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("No stamina"), FLinearColor(0.5f, 0.82f, 0.5f), 0.8f);
		return;
	}
	const FRotator Yaw = MoveFrame();
	const FVector Wish = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * MoveInput.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * MoveInput.X;
	// No WASD: top-down rolls toward the cursor (or along a click-to-move path), third person forward.
	FVector Fallback = GetActorForwardVector();
	if (bTopDown)
	{
		if (Goal != EClickGoal::None && Path.IsValidIndex(PathIndex)) Fallback = (Path[PathIndex] - GetActorLocation()).GetSafeNormal2D();
		else if (const FVector To = (AimPoint() - GetActorLocation()).GetSafeNormal2D(); !To.IsNearlyZero()) Fallback = To;
	}
	const FVector Dir = Wish.IsNearlyZero() ? Fallback : Wish.GetSafeNormal();
	Tags.Add(TEXT("Invulnerable"), float(Get(TEXT("iframes"), 0.26)));
	StartDash(Dir, D.Px(Get(TEXT("speed"), 430)), float(Get(TEXT("duration"), 0.32)), nullptr);
	if (UAnimSequenceBase* Dash = RPGAssets::Load<UAnimSequenceBase>(RPGAssets::DashAnim))
		if (UAnimInstance* A = Anim()) A->PlaySlotAnimationAsDynamicMontage(Dash, TEXT("DefaultSlot"), 0.05f, 0.15f, 1.25f);
}

void ARPGPlayerCharacter::StartDash(const FVector& Dir, float Speed, float Duration, const RPGJson::FObj& Strike)
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
	for (const FRPGItem& It : Inventory->Items)
	{
		if (It.Id != TEXT("potion")) continue;
		if (!Inventory->Use(It.Uid)) URPGStory::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("Already at full health"), FLinearColor(0.67f, 0.67f, 0.73f), 0.8f);
		return;
	}
	URPGStory::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("No potions"), FLinearColor(0.67f, 0.67f, 0.73f), 0.8f);
}

void ARPGPlayerCharacter::SwapStyle()
{
	const int32 N = RPGJson::Arr(ClassDef, TEXT("styles")).Num();
	if (N < 2 || bAttacking || IsDodging()) return;
	StyleIndex = (StyleIndex + 1) % N;
	ComboStep = 0;
	bGuardHeld = bDrawing = false;
	RefreshWeapons();
	URPGStory::Get(this)->Toast(TEXT("Switched to ") + RPGJson::Str(Style(), TEXT("name")), NameColor);
}

USkinnedMeshComponent* ARPGPlayerCharacter::BodyMesh() const { return PoseMesh; }

void ARPGPlayerCharacter::RefreshWeapons()
{
	const TArray<TSharedPtr<FJsonValue>> Styles = RPGJson::Arr(ClassDef, TEXT("styles"));
	if (Styles.IsEmpty()) return;
	TArray<FString> Kits;
	for (const TSharedPtr<FJsonValue>& V : RPGJson::Arr(RPGJson::Obj(URPGData::Get(this).World3D(), TEXT("styleKits")), Styles[FMath::Clamp(StyleIndex, 0, Styles.Num() - 1)]->AsString())) Kits.Add(V->AsString());
	SetWeaponKits(Kits);
}

void ARPGPlayerCharacter::Restore()
{
	Stats->Fill();
	ARPGFX::Ring(GetWorld(), GetActorLocation() - FVector(0, 0, 86), 120.f, FLinearColor(0.37f, 0.88f, 0.54f), 0.5f);
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
	ClearGoal();
	ClosePicker(false);
	SetTalkMode(false);
	DeathTimer = 3.f;
	ShieldBubble->SetVisibility(false);
	GuardArc->SetVisibility(false);
	AimLine->SetVisibility(false);
}

void ARPGPlayerCharacter::Respawn()
{
	URPGStory* Story = URPGStory::Get(this);
	const int32 Lost = FMath::FloorToInt(Inventory->Gold * URPGData::Get(this).Tuning(TEXT("deathGoldPenalty"), 0.1));
	Inventory->Gold -= Lost;
	bDead = false;
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	SetActorLocation(SpawnPoint, false, nullptr, ETeleportType::TeleportPhysics);
	Tags.Clear();
	Stats->Effects.Reset();
	Stats->Fill();
	KnockVelocity = FVector::ZeroVector;
	for (TActorIterator<ARPGEnemy> It(GetWorld()); It; ++It) if (!It->IsDead() && !It->IsLeaving()) It->ResetToHome();
	if (Story->Duel.IsValid()) { Story->Duel = nullptr; Story->SetFlag(TEXT("duel_lost")); Story->Toast(TEXT("You lost the duel."), FLinearColor(1.f, 0.5f, 0.5f)); }
	Story->Toast(Lost ? FString::Printf(TEXT("You lost %d gold."), Lost) : FString(TEXT("You wake in the village.")), FLinearColor(1.f, 0.5f, 0.5f));
}

// ---------------------------------------------------------------------------------------------

void ARPGPlayerCharacter::UpdateVisuals(float Dt)
{
	// The Mage's barrier (held RMB): a bubble engulfs him, fading as his stamina runs down.
	const RPGJson::FObj Guarding = GuardStyle();
	const bool bShield = !bDead && Guarding && RPGJson::Bool(Guarding, TEXT("barrier"));
	ShieldBubble->SetVisibility(bShield);
	if (bShield)
	{
		// Fully around the hero: centred on the drawn sprite (taller than the old mannequin), sized to enclose it.
		if (Sprite)
		{
			ShieldBubble->SetWorldLocation(Sprite->GetComponentLocation());
			const float D = Sprite->GetComponentScale().Y * 100.f * 1.25f;   // the card's height, plus a margin
			ShieldBubble->SetWorldScale3D(FVector(D / 100.f));
		}
		BarrierPulse += Dt;
		BubbleMat->SetScalarParameterValue(TEXT("Opacity"), 0.12f + 0.2f * Stats->Stamina / FMath::Max(1.f, Stats->MaxStamina()) + 0.04f * FMath::Sin(BarrierPulse * 6.f));
		BubbleMat->SetScalarParameterValue(TEXT("Intensity"), 3.f);
	}

	// Raised guard: the left arm comes up into a guard pose (procedural, until real block
	// animations arrive) and the shield on the forearm rises with it; a brief white flash marks the perfect-block window.
	const RPGJson::FObj Guard = GuardStyle();
	const float Perfect = Guard ? float(RPGJson::Num(Guard, TEXT("perfectWindow"), 0)) : 0.f;
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
	URPGStory* Story = URPGStory::Get(this);

	// Camera shake from hits.
	ShakeOffset = Story->ShakeAmount > 0.2f ? FVector(0, FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * Story->ShakeAmount : FVector::ZeroVector;
	Camera->SetRelativeLocation(ShakeOffset);
	if (bTopDown && RPGLook::Mode() == RPGLook::EMode::Flat2D) Camera->SetOrthoWidth(FMath::FInterpTo(Camera->OrthoWidth, ZoomTarget, Dt, 8.f));
	else if (bTopDown) CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, ZoomTarget, Dt, 8.f);
	if (bTopDown) Camera->PostProcessSettings.DepthOfFieldFocalDistance = CameraBoom->TargetArmLength;   // keep the hero in focus while zooming
	if (NightGlow) NightGlow->SetIntensity(5.f * ARPGWorldBuilder::Night());


	if (bDead)
	{
		DeathTimer -= Dt;
		if (DeathTimer <= 0.f) Respawn();
		return;
	}

	Stats->StaminaRegenMul = 1.f;
	Stats->TickStats(Dt, [this](float H) { RPGCombat::Heal(this, H); }, [this](float D, AActor* Src) { RPGCombat::Dot(this, D, Src); });
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
			for (ARPGCharacterBase* E : RPGCombat::Opponents(this))
			{
				if (DashHit.Contains(E) || FVector::Dist2D(E->GetActorLocation(), GetActorLocation()) > Radius() + E->Radius() + 40.f) continue;
				DashHit.Add(E);
				FRPGHit H;
				H.Base = float(RPGJson::Num(DashStrike, TEXT("damage"), 16));
				H.Scaling = FName(RPGJson::Str(DashStrike, TEXT("scaling")));
				H.Poise = float(RPGJson::Num(DashStrike, TEXT("poise"), 30));
				H.Knockback = float(RPGJson::Num(DashStrike, TEXT("knockback"), 100));
				H.Dir = DodgeDir;
				RPGCombat::Deal(this, E, H);
			}
		}
		return;
	}
	if (Tags.Has(TEXT("Staggered"))) return;

	// Hold LMB to keep attacking (bolt fires on cooldown; melee restarts the combo). A click-to-attack
	// goal does its own attacking once in range.
	if (bAttackHeld && !bAttacking && Goal == EClickGoal::None)
	{
		const RPGJson::FObj Primary = RPGJson::Obj(Style(), TEXT("primary"));
		if (RPGJson::Str(Primary, TEXT("type")) == TEXT("bolt")) { if (BoltCooldown <= 0.f) FireBolt(); }
		else StartCombo();
	}

	// Strafe toward the camera while blocking / drawing / casting; otherwise turn toward movement.
	const RPGJson::FObj Sec = RPGJson::Obj(Style(), TEXT("secondary"));
	const bool bAiming = GuardStyle().IsValid() || bDrawing || CastSlow > 0.f;
	GetCharacterMovement()->bOrientRotationToMovement = !bAiming && !bAttacking;
	if (bAiming && GuardStyle().IsValid() && !bDrawing) FaceThreat();   // the shield turns toward who's coming at you
	else if (bAiming) FaceAim();
	if (const RPGJson::FObj G = GuardStyle())
	{
		// Holding a shield (or the Mage's barrier) wears you out: stamina drains, and when it's gone the guard drops.
		Stats->StaminaRegenMul = float(RPGJson::Num(G, TEXT("staminaRegenMul"), 0.35));
		const float Drain = float(RPGJson::Num(G, TEXT("staminaDrain"), 0)) * Dt;
		if (Drain > 0.f)
		{
			Stats->Stamina = FMath::Max(0.f, Stats->Stamina - Drain);
			Stats->StaminaDelay = FMath::Max(Stats->StaminaDelay, 0.3f);
			if (Stats->Stamina <= 0.f)
			{
				bGuardHeld = false;
				URPGStory::Get(this)->Float(Head() + FVector(0, 0, 30), TEXT("Too tired to block"), FLinearColor(0.5f, 0.82f, 0.5f), 0.8f);
			}
		}
	}

	// Movement: WASD / stick (cancels any click-to-move), or the click-to-move path.
	if (!MoveInput.IsNearlyZero()) ClearGoal();
	const FVector ClickDir = UpdateClickGoal(Dt);
	if (Controller && (!MoveInput.IsNearlyZero() || !ClickDir.IsNearlyZero()))
	{
		float Mul = 1.f;
		if (bAttacking) Mul = float(RPGJson::Num(URPGData::Get(this).Section(TEXT("combat")), TEXT("attackMoveMul"), 0.35));
		else if (bAiming && (GuardStyle().IsValid() || bDrawing)) Mul = float(RPGJson::Num(Sec, TEXT("moveMul"), 0.5));
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
