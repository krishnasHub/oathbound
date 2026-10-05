#include "RPGPoseMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

URPGPoseMesh::URPGPoseMesh()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After animation (including parallel evaluation) has finished for the frame.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void URPGPoseMesh::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USkeletalMeshComponent* Src = Source.Get();
	if (!Src || !GetSkinnedAsset() || !GetOwner()) return;

	CopyPoseFromSkeletalComponent(Src);

	// Target per bone from the active poses (later poses override earlier ones).
	TMap<FName, FQuat> Wanted;
	for (const FName& PoseName : ActivePoses)
	{
		if (const TArray<FBoneTarget>* Pose = Poses.Find(PoseName))
		{
			for (const FBoneTarget& T : *Pose)
				Wanted.Add(T.Bone, FRotationMatrix::MakeFromXZ(T.AxisX.GetSafeNormal(), T.AxisZHint.GetSafeNormal()).ToQuat());
		}
	}

	// Bones to touch: everything wanted, plus anything still easing back to the animation.
	TArray<FName> Bones;
	Wanted.GetKeys(Bones);
	for (const auto& KV : Current) Bones.AddUnique(KV.Key);
	if (Bones.IsEmpty()) { if (OnPosed) OnPosed(); return; }
	// Parents before children (bone indices are ordered that way), so each child sees its posed parent.
	Bones.Sort([this](const FName& A, const FName& B) { return GetBoneIndex(A) < GetBoneIndex(B); });

	const FQuat Actor = GetOwner()->GetActorQuat();
	const float K = 1.f - FMath::Exp(-BlendSpeed * DeltaTime);
	for (const FName& Bone : Bones)
	{
		const FQuat Animated = Actor.Inverse() * GetBoneRotationByName(Bone, EBoneSpaces::WorldSpace).Quaternion();
		const FQuat* Target = Wanted.Find(Bone);
		FQuat& Shown = Current.FindOrAdd(Bone, Animated);
		Shown = FQuat::Slerp(Shown, Target ? *Target : Animated, K);
		if (!Target && Shown.AngularDistance(Animated) < FMath::DegreesToRadians(0.5f))
		{
			Current.Remove(Bone);   // back to pure animation
			continue;
		}
		SetBoneRotationByName(Bone, (Actor * Shown).Rotator(), EBoneSpaces::WorldSpace);
	}
	MarkRefreshTransformDirty();
	if (OnPosed) OnPosed();
}
