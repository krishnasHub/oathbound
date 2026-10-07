#include "RPGGameMode.h"
#include "TSLook.h"
#include "RPGAmbient.h"
#include "Oathbound.h"
#include "TSData.h"
#include "RPGAssets.h"
#include "RPGWorldBuilder.h"
#include "RPGPlayerCharacter.h"
#include "RPGPlayerController.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "RPGHUD.h"
#include "RPGSelfTest.h"
#include "TSTestRunner.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "HAL/PlatformMisc.h"

ARPGGameMode::ARPGGameMode()
{
	DefaultPawnClass = ARPGPlayerCharacter::StaticClass();
	PlayerControllerClass = ARPGPlayerController::StaticClass();
	HUDClass = ARPGHUD::StaticClass();
}

void ARPGGameMode::StartPlay()
{
	TSLook::Init(this);   // before anything builds or spawns: it decides how they look
	// Material names in the data (kit parts, "coin") are Starter Content materials, or their pixel-art versions.
	TSAssets::SetMaterialResolver([](const FString& Name) { return RPGAssets::StarterMat(Name); });
	WorldBuilder = GetWorld()->SpawnActor<ARPGWorldBuilder>();
	WorldBuilder->Build();
	if (ARPGAmbient* Life = GetWorld()->SpawnActor<ARPGAmbient>()) Life->Init(WorldBuilder);   // birds, geese, prowler, fireflies
	SpawnCharacters();

	Super::StartPlay();
	RunSelfTests();
}

void ARPGGameMode::SpawnCharacters()
{
	const UTSData& D = UTSData::Get(this);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	int32 Npcs = 0, Enemies = 0;
	for (const FTSSpawn& S : D.Spawns)
	{
		FVector At = D.TileCenter(S.X, S.Y);
		At.Z = WorldBuilder->GroundZ(At.X, At.Y);
		const FVector Above = At + FVector(0, 0, 130.f);
		if (S.Kind == TEXT("npc"))
		{
			if (ARPGNPC* N = GetWorld()->SpawnActor<ARPGNPC>(Above, FRotator(0, 90, 0), SP)) { N->Init(S.Id); ++Npcs; }
		}
		else if (S.Kind == TEXT("enemy"))
		{
			// Bandits at the bridge face north toward the road; others face a random way.
			const float Yaw = D.Entry(TEXT("enemies"), S.Id)->HasField(TEXT("faction")) ? -90.f : FMath::FRandRange(0.f, 360.f);
			if (ARPGEnemy* E = GetWorld()->SpawnActor<ARPGEnemy>(Above, FRotator(0, Yaw, 0), SP)) { E->Init(S.Id, At); ++Enemies; }
		}
	}
	UE_LOG(LogRPG, Display, TEXT("Spawned %d villagers and %d enemies."), Npcs, Enemies);
}

void ARPGGameMode::RestartPlayer(AController* NewPlayer)
{
	const UTSData& D = UTSData::Get(this);
	FVector Start(0, 0, 200);
	for (const FTSSpawn& S : D.Spawns)
	{
		if (S.Kind == TEXT("player")) { Start = D.TileCenter(S.X, S.Y) + FVector(0, 0, 160); break; }
	}
	// Face east, toward the meadow and the bridge.
	RestartPlayerAtTransform(NewPlayer, FTransform(FRotator(0, 0, 0), Start));

	if (ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(NewPlayer->GetPawn()))
	{
		FString Class = TEXT("knight"), Sex = TEXT("male");
		FParse::Value(FCommandLine::Get(), TEXT("RPGClass="), Class);
		FParse::Value(FCommandLine::Get(), TEXT("RPGSex="), Sex);
		P->ClassId = Class;
		P->Sex = Sex;
	}
}

void ARPGGameMode::RunSelfTests()
{
	const TCHAR* Cmd = FCommandLine::Get();

	if (FParse::Param(Cmd, TEXT("RPGProbe"))) Probe();

	// -RPGTest= / -RPGShot= / -RPGCam= / -RPGQuitAfter= (Tessera's automation switches, with this game's runner).
	TSTestSwitches::Run(GetWorld(), ARPGSelfTest::StaticClass());

	// -RPGLowHP: drop the hero to 15% health after 3 s (to see the low-health warning).
	if (FParse::Param(Cmd, TEXT("RPGLowHP")))
	{
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [this]()
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				if (ARPGPlayerCharacter* P = Cast<ARPGPlayerCharacter>(PC->GetPawn())) P->Stats->Health() = P->Stats->MaxHealth() * 0.15f;
		}, 3.f, false);
	}

}

void ARPGGameMode::Probe()
{
	// Bone axes in the character's own space (X forward, Y right, Z up), in the idle pose, so weapon
	// mounts can be computed instead of guessed. Logged a moment later, once the pose has evaluated.
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, [this]()
	{
		ACharacter* C = Cast<ACharacter>(GetWorld()->GetFirstPlayerController()->GetPawn());
		if (!C) return;
		for (const TCHAR* Bone : { TEXT("hand_r"), TEXT("hand_l"), TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("pelvis"), TEXT("spine_03"), TEXT("spine_05"), TEXT("thigh_r") })
		{
			const FTransform B = C->GetMesh()->GetSocketTransform(Bone);
			const FTransform A = C->GetActorTransform();
			auto L = [&](EAxis::Type Ax) { return A.InverseTransformVectorNoScale(B.GetUnitAxis(Ax)); };
			UE_LOG(LogRPG, Display, TEXT("PROBE bone %s: X=%s Y=%s Z=%s  pos=%s"), Bone,
				*L(EAxis::X).ToCompactString(), *L(EAxis::Y).ToCompactString(), *L(EAxis::Z).ToCompactString(),
				*A.InverseTransformPosition(B.GetLocation()).ToCompactString());
		}
	}, 2.f, false);

	// Without the editor, this is how we learn which parameters a material exposes.
	const TCHAR* Paths[] = {
		TEXT("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_01_New.MI_Manny_01_New"),
		TEXT("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_02_New.MI_Manny_02_New"),
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
		TEXT("/Game/StarterContent/Materials/M_Ground_Grass.M_Ground_Grass"),
		TEXT("/Game/StarterContent/Materials/M_Water_Lake.M_Water_Lake"),
	};
	for (const TCHAR* Path : Paths)
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!M) { UE_LOG(LogRPG, Display, TEXT("PROBE %s: not found"), Path); continue; }
		TArray<FMaterialParameterInfo> Info; TArray<FGuid> Ids;
		FString Vec, Scal, Tex;
		M->GetAllVectorParameterInfo(Info, Ids);  for (auto& I : Info) Vec += I.Name.ToString() + TEXT(" ");
		M->GetAllScalarParameterInfo(Info, Ids);  for (auto& I : Info) Scal += I.Name.ToString() + TEXT(" ");
		M->GetAllTextureParameterInfo(Info, Ids); for (auto& I : Info) Tex += I.Name.ToString() + TEXT(" ");
		UE_LOG(LogRPG, Display, TEXT("PROBE %s\n   vectors: %s\n   scalars: %s\n   textures: %s"), Path, *Vec, *Scal, *Tex);
	}
}
