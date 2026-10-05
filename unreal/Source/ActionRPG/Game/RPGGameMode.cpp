#include "RPGGameMode.h"
#include "ActionRPG.h"
#include "RPGData.h"
#include "RPGAssets.h"
#include "RPGWorldBuilder.h"
#include "RPGPlayerCharacter.h"
#include "RPGPlayerController.h"
#include "RPGEnemy.h"
#include "RPGNPC.h"
#include "RPGHUD.h"
#include "RPGSelfTest.h"

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
	WorldBuilder = GetWorld()->SpawnActor<ARPGWorldBuilder>();
	WorldBuilder->Build();
	SpawnCharacters();

	Super::StartPlay();
	RunSelfTests();
}

void ARPGGameMode::SpawnCharacters()
{
	const URPGData& D = URPGData::Get(this);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	int32 Npcs = 0, Enemies = 0;
	for (const FRPGSpawn& S : D.Spawns)
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
	const URPGData& D = URPGData::Get(this);
	FVector Start(0, 0, 200);
	for (const FRPGSpawn& S : D.Spawns)
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

	FString CamSpec;
	if (FParse::Value(Cmd, TEXT("RPGCam="), CamSpec, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> P;
		CamSpec.ParseIntoArray(P, TEXT(","));
		if (P.Num() == 5)
		{
			const FVector Loc(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
			const FRotator Rot(FCString::Atof(*P[3]), FCString::Atof(*P[4]), 0);
			ACameraActor* CamActor = GetWorld()->SpawnActor<ACameraActor>(Loc, Rot);
			CamActor->GetCameraComponent()->SetFieldOfView(70.f);
			CamActor->GetCameraComponent()->bConstrainAspectRatio = false;
			// The controller re-targets its pawn on possession, so take over the view a moment later.
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, [this, CamActor]()
			{
				if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
				{
					PC->bAutoManageActiveCameraTarget = false;
					PC->SetViewTarget(CamActor);
				}
			}, 0.25f, false);
		}
	}

	float ShotAt = 0.f;
	if (FParse::Value(Cmd, TEXT("RPGShot="), ShotAt))
	{
		FString Name = TEXT("shot");
		FParse::Value(Cmd, TEXT("RPGShotName="), Name);
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/RPG") / Name + TEXT(".png"));
		FTimerHandle H;
		GetWorldTimerManager().SetTimer(H, [File]()
		{
			FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogRPG, Display, TEXT("Screenshot requested: %s"), *File);
		}, ShotAt, false);
		FTimerHandle Q;
		GetWorldTimerManager().SetTimer(Q, []() { FPlatformMisc::RequestExit(false); }, ShotAt + 2.f, false);
	}

	FString Scenario;
	if (FParse::Value(Cmd, TEXT("RPGTest="), Scenario))
	{
		ARPGSelfTest* Test = GetWorld()->SpawnActor<ARPGSelfTest>();
		Test->Scenario = Scenario;
		UE_LOG(LogRPG, Display, TEXT("Running self-test scenario '%s'"), *Scenario);
	}

	float QuitAfter = 0.f;
	if (FParse::Value(Cmd, TEXT("RPGQuitAfter="), QuitAfter))
	{
		FTimerHandle Q;
		GetWorldTimerManager().SetTimer(Q, []() { UE_LOG(LogRPG, Display, TEXT("Self-test run complete.")); FPlatformMisc::RequestExit(false); }, QuitAfter, false);
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
