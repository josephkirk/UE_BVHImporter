#include "BVHFactory.h"
#include "BVHParser.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ReferenceSkeleton.h"
#include "Misc/FeedbackContext.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshUtilities.h"
#include "Materials/Material.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"

UBVHFactory::UBVHFactory()
{
	SupportedClass = UAnimSequence::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	Formats.Add(TEXT("bvh;Biovision Hierarchy"));
}

bool UBVHFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("bvh"), ESearchCase::IgnoreCase);
}

// Convert BVH (Y-Up, Right-Handed) to UE (Z-Up, Left-Handed)
// Mapping: UE_X = BVH_X, UE_Y = -BVH_Z, UE_Z = BVH_Y
FVector ConvertPos(const FVector3d& InPos)
{
	return FVector(InPos.X, -InPos.Z, InPos.Y);
}

FQuat ConvertRot(const FQuat& InRot)
{
// Convert quaternion from BVH space to UE space
	return FQuat(InRot.X, -InRot.Z, InRot.Y, InRot.W);
}

void BuildSkeletonHierarchy(const TSharedPtr<FBVHNode>& Node, FReferenceSkeletonModifier& Modifier, const FName& ParentName, TMap<FString, FName>& OutBoneMap)
{
	if (!Node.IsValid()) return;

	FString BoneNameStr = Node->Name;
	if (BoneNameStr.IsEmpty()) BoneNameStr = TEXT("Joint");
	
	// Ensure unique names?
	FName BoneName = FName(*BoneNameStr);
	
	FMeshBoneInfo BoneInfo(BoneName, BoneName.ToString(), Modifier.FindBoneIndex(ParentName));
	
	// Transform
	// BVH Offset is local translation from parent
	FTransform BoneTransform;
	BoneTransform.SetLocation(ConvertPos(Node->Offset));
	BoneTransform.SetRotation(FQuat::Identity); // Base pose usually has 0 rotation in BVH
	BoneTransform.SetScale3D(FVector::OneVector);

	Modifier.Add(BoneInfo, BoneTransform);
	
	OutBoneMap.Add(Node->Name, BoneName);

	for (const auto& Child : Node->Children)
	{
		BuildSkeletonHierarchy(Child, Modifier, BoneName, OutBoneMap);
	}
}

UObject* UBVHFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	UE_LOG(LogTemp, Log, TEXT("BVHFactory: Starting import of %s"), *Filename);

	FBVHParser Parser(Filename);
	FBVHData Data;
	if (!Parser.Parse(Data))
	{
		UE_LOG(LogTemp, Error, TEXT("BVHFactory: Failed to parse BVH file."));
		Warn->Log(ELogVerbosity::Error, TEXT("Failed to parse BVH file."));
		return nullptr;
	}

	if (!Data.RootNode.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("BVHFactory: RootNode is invalid after parsing."));
		return nullptr;
	}

	UE_LOG(LogTemp, Log, TEXT("BVHFactory: Parsing successful. RootNode: %s, Frames: %d"), *Data.RootNode->Name, Data.NumFrames);

	// 1. Create Skeleton
	UE_LOG(LogTemp, Log, TEXT("BVHFactory: Creating Skeleton..."));
	FString SkeletonName = InName.ToString() + TEXT("_Skeleton");
	FString SkeletonPackageName = FPaths::Combine(FPaths::GetPath(InParent->GetPathName()), SkeletonName);
	UPackage* SkeletonPackage = CreatePackage(*SkeletonPackageName);
	USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, FName(*SkeletonName), Flags | RF_Public | RF_Standalone | RF_Transactional);
	
	// Build Reference Skeleton locally first
	FReferenceSkeleton LocalRefSkeleton;
	TMap<FString, FName> BoneMap; // BVH Node Name -> UE Bone Name
	{
		FReferenceSkeletonModifier Modifier(LocalRefSkeleton, nullptr);
		
		UE_LOG(LogTemp, Log, TEXT("BVHFactory: Building Skeleton Hierarchy..."));
		BuildSkeletonHierarchy(Data.RootNode, Modifier, NAME_None, BoneMap);
		UE_LOG(LogTemp, Log, TEXT("BVHFactory: Hierarchy built. Bone count: %d"), BoneMap.Num());
	}
	
	UE_LOG(LogTemp, Log, TEXT("BVHFactory: LocalRefSkeleton bone count: %d"), LocalRefSkeleton.GetNum());
	if (LocalRefSkeleton.GetNum() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("BVHFactory: LocalRefSkeleton is empty!"));
		return nullptr;
	}
	
	// 2. Create Skeletal Mesh (Dummy)
	UE_LOG(LogTemp, Log, TEXT("BVHFactory: Creating Skeletal Mesh..."));
	FString MeshName = InName.ToString() + TEXT("_Mesh");
	FString MeshPackageName = FPaths::Combine(FPaths::GetPath(InParent->GetPathName()), MeshName);
	UPackage* MeshPackage = CreatePackage(*MeshPackageName);
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(MeshPackage, FName(*MeshName), Flags | RF_Public | RF_Standalone | RF_Transactional);
	SkeletalMesh->SetSkeleton(Skeleton);
	
	// Create a dummy triangle to satisfy engine requirements for skeletal meshes
	FSkeletalMeshImportData ImportData;
	ImportData.Points.Add(FVector3f(0, 0, 0));
	ImportData.Points.Add(FVector3f(0, 1, 0));
	ImportData.Points.Add(FVector3f(0, 0, 1));
	SkeletalMeshImportData::FVertex V0, V1, V2;
	V0.VertexIndex = 0; V1.VertexIndex = 1; V2.VertexIndex = 2;
	V0.MatIndex = 0; V1.MatIndex = 0; V2.MatIndex = 0;
	V0.UVs[0] = FVector2f(0, 0); V1.UVs[0] = FVector2f(1, 0); V2.UVs[0] = FVector2f(0, 1);
	
	SkeletalMeshImportData::FTriangle Tri;
	Tri.WedgeIndex[0] = 0; Tri.WedgeIndex[1] = 1; Tri.WedgeIndex[2] = 2;
	Tri.MatIndex = 0;
	Tri.AuxMatIndex = 0;
	Tri.SmoothingGroups = 0;
	Tri.TangentZ[0] = FVector3f(0,0,1); Tri.TangentZ[1] = FVector3f(0,0,1); Tri.TangentZ[2] = FVector3f(0,0,1);
	Tri.TangentX[0] = FVector3f(1,0,0); Tri.TangentX[1] = FVector3f(1,0,0); Tri.TangentX[2] = FVector3f(1,0,0);
	Tri.TangentY[0] = FVector3f(0,1,0); Tri.TangentY[1] = FVector3f(0,1,0); Tri.TangentY[2] = FVector3f(0,1,0);
	
	ImportData.Wedges.Add(V0);
	ImportData.Wedges.Add(V1);
	ImportData.Wedges.Add(V2);
	ImportData.Faces.Add(Tri);
	
	ImportData.PointToRawMap.Add(0);
	ImportData.PointToRawMap.Add(1);
	ImportData.PointToRawMap.Add(2);
	
	// Weight to root bone (Index 0)
	SkeletalMeshImportData::FRawBoneInfluence Inf0, Inf1, Inf2;
	Inf0.BoneIndex = 0; Inf0.VertexIndex = 0; Inf0.Weight = 1.0f;
	Inf1.BoneIndex = 0; Inf1.VertexIndex = 1; Inf1.Weight = 1.0f;
	Inf2.BoneIndex = 0; Inf2.VertexIndex = 2; Inf2.Weight = 1.0f;
	ImportData.Influences.Add(Inf0);
	ImportData.Influences.Add(Inf1);
	ImportData.Influences.Add(Inf2);
	
	// Materials
	SkeletalMeshImportData::FMaterial Mat;
	Mat.MaterialImportName = TEXT("DummyMat");
	ImportData.Materials.Add(Mat);
	
	// Populate RefBonesBinary from Skeleton
	const TArray<FMeshBoneInfo>& RefBoneInfos = LocalRefSkeleton.GetRefBoneInfo();
	const TArray<FTransform>& RefBonePose = LocalRefSkeleton.GetRefBonePose();
	
	for (int32 i = 0; i < RefBoneInfos.Num(); ++i)
	{
		SkeletalMeshImportData::FBone Bone;
		Bone.Name = RefBoneInfos[i].Name.ToString();
		Bone.Flags = 0;
		Bone.ParentIndex = RefBoneInfos[i].ParentIndex;
		Bone.NumChildren = 0; // Will calculate below
		
		FTransform BoneTransform = RefBonePose[i];
		Bone.BonePos.Transform = FTransform3f(BoneTransform);
		Bone.BonePos.Length = 1.0f; 
		Bone.BonePos.XSize = 1.0f;
		Bone.BonePos.YSize = 1.0f;
		Bone.BonePos.ZSize = 1.0f;
		
		ImportData.RefBonesBinary.Add(Bone);
	}
	
	// Calculate NumChildren
	for (int32 i = 0; i < ImportData.RefBonesBinary.Num(); ++i)
	{
		int32 ParentIdx = ImportData.RefBonesBinary[i].ParentIndex;
		if (ParentIdx != INDEX_NONE && ParentIdx < ImportData.RefBonesBinary.Num())
		{
			ImportData.RefBonesBinary[ParentIdx].NumChildren++;
		}
	}
	Skeleton->SetPreviewMesh(SkeletalMesh);
	Skeleton->PostEditChange();

	FAssetRegistryModule::AssetCreated(Skeleton);
	FAssetRegistryModule::AssetCreated(SkeletalMesh);

	// 3. Create AnimSequence
	UE_LOG(LogTemp, Log, TEXT("BVHFactory: Creating AnimSequence..."));
	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(InParent, InName, Flags | RF_Public | RF_Standalone | RF_Transactional);
	AnimSequence->SetSkeleton(Skeleton);
	AnimSequence->SetPreviewMesh(SkeletalMesh);
	
	// Flatten tree in DFS order to match channel data
	TArray<TSharedPtr<FBVHNode>> FlatNodes;
	
	struct FNodeCollector
	{
		static void Collect(TSharedPtr<FBVHNode> Node, TArray<TSharedPtr<FBVHNode>>& OutArray)
		{
			if (!Node.IsValid()) return;
			OutArray.Add(Node);
			for (auto Child : Node->Children)
			{
				Collect(Child, OutArray);
			}
		}
	};
	FNodeCollector::Collect(Data.RootNode, FlatNodes);
	UE_LOG(LogTemp, Log, TEXT("BVHFactory: Flattened nodes. Count: %d"), FlatNodes.Num());
	
	// Assign start indices
	int32 CurrentChannelIdx = 0;
	for (auto Node : FlatNodes)
	{
		Node->ChannelStartIndex = CurrentChannelIdx;
		CurrentChannelIdx += Node->Channels.Num();
	}
	
	IAnimationDataController& Controller = AnimSequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Import BVH")));
	
	AnimSequence->ImportFileFramerate = 1.0 / Data.FrameTime;
	AnimSequence->ImportResampleFramerate = 1.0 / Data.FrameTime;
	Controller.SetFrameRate(FFrameRate(FMath::RoundToInt(1.0 / Data.FrameTime), 1));
	Controller.SetNumberOfFrames(Data.NumFrames);
	
	for (auto Node : FlatNodes)
	{
		if (!Node.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("BVHFactory: Found invalid node in FlatNodes."));
			continue;
		}

		FName BoneName = BoneMap[Node->Name];
		if (BoneName == NAME_None)
		{
			UE_LOG(LogTemp, Warning, TEXT("BVHFactory: Node %s not found in BoneMap."), *Node->Name);
			continue;
		}
		
		Controller.AddBoneCurve(BoneName);
		
		TArray<FVector> PosKeys;
		TArray<FQuat> RotKeys;
		PosKeys.Reserve(Data.NumFrames);
		RotKeys.Reserve(Data.NumFrames);
		
		for (int32 Frame = 0; Frame < Data.NumFrames; ++Frame)
		{
			const TArray<double>& FrameValues = Data.MotionData[Frame];
			
			FVector3d LocalPos = Node->Offset;
			
			bool bHasPos = false;
			FVector3d ChanPos = FVector3d::ZeroVector;
			
			// Process channels in order
			for (int32 i = 0; i < Node->Channels.Num(); ++i)
			{
				double Val = FrameValues[Node->ChannelStartIndex + i];
				EBVHChannel Chan = Node->Channels[i];
				
				switch (Chan)
				{
				case EBVHChannel::Xposition: ChanPos.X = Val; bHasPos = true; break;
				case EBVHChannel::Yposition: ChanPos.Y = Val; bHasPos = true; break;
				case EBVHChannel::Zposition: ChanPos.Z = Val; bHasPos = true; break;
				case EBVHChannel::Zrotation: 
					// Rotation handled in second pass
					break;
				case EBVHChannel::Xrotation:
				case EBVHChannel::Yrotation:
					break;
				}
			}
			
			// Compose local rotation from channels
			FQuat LocalRot = FQuat::Identity;
			for (int32 i = 0; i < Node->Channels.Num(); ++i)
			{
				double Val = FrameValues[Node->ChannelStartIndex + i];
				EBVHChannel Chan = Node->Channels[i];
				FQuat ChanRot = FQuat::Identity;
				
				if (Chan == EBVHChannel::Xrotation) ChanRot = FQuat(FVector(1,0,0), FMath::DegreesToRadians(Val));
				else if (Chan == EBVHChannel::Yrotation) ChanRot = FQuat(FVector(0,1,0), FMath::DegreesToRadians(Val));
				else if (Chan == EBVHChannel::Zrotation) ChanRot = FQuat(FVector(0,0,1), FMath::DegreesToRadians(Val));
				
				if (!ChanRot.IsIdentity())
				{
					LocalRot = LocalRot * ChanRot;
				}
			}
			
			if (bHasPos)
			{
				LocalPos = ChanPos;
			}
			
			PosKeys.Add(ConvertPos(LocalPos));
			RotKeys.Add(ConvertRot(LocalRot));
		}
		
		Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, TArray<FVector>());
	}
	
	Controller.CloseBracket();
	
	// Notify Asset Registry
	FAssetRegistryModule::AssetCreated(AnimSequence);
	
	return AnimSequence;
}
