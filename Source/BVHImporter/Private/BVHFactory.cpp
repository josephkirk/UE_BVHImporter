#include "BVHFactory.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationSettings.h"
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BVHParser.h"
#include "Engine/SkeletalMesh.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "MeshUtilities.h"
#include "Misc/FeedbackContext.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshAttributes.h"
#include "UObject/SavePackage.h"


UBVHFactory::UBVHFactory() {
  SupportedClass = UAnimSequence::StaticClass();
  bCreateNew = false;
  bEditorImport = true;
  Formats.Add(TEXT("bvh;Biovision Hierarchy"));
}

bool UBVHFactory::FactoryCanImport(const FString &Filename) {
  return FPaths::GetExtension(Filename).Equals(TEXT("bvh"),
                                               ESearchCase::IgnoreCase);
}

// Convert BVH (Y-Up, Right-Handed) to UE (Z-Up, Left-Handed)
// Mapping: UE_X = BVH_X, UE_Y = -BVH_Z, UE_Z = BVH_Y
FVector ConvertPos(const FVector3d &InPos) {
  return FVector(InPos.X, -InPos.Z, InPos.Y);
}

FQuat ConvertRot(const FQuat &InRot) {
  // Convert quaternion from BVH space to UE space
  return FQuat(InRot.X, -InRot.Z, InRot.Y, InRot.W);
}

void BuildSkeletonHierarchy(const TSharedPtr<FBVHNode> &Node,
                            FReferenceSkeletonModifier &Modifier,
                            const FName &ParentName,
                            TMap<FString, FName> &OutBoneMap) {
  if (!Node.IsValid())
    return;

  FString BoneNameStr = Node->Name;
  if (BoneNameStr.IsEmpty())
    BoneNameStr = TEXT("Joint");

  // Ensure unique names?
  FName BoneName = FName(*BoneNameStr);

  FMeshBoneInfo BoneInfo(BoneName, BoneName.ToString(),
                         Modifier.FindBoneIndex(ParentName));

  // Transform
  // BVH Offset is local translation from parent
  FTransform BoneTransform;
  BoneTransform.SetLocation(ConvertPos(Node->Offset));
  BoneTransform.SetRotation(
      FQuat::Identity); // Base pose usually has 0 rotation in BVH
  BoneTransform.SetScale3D(FVector::OneVector);

  Modifier.Add(BoneInfo, BoneTransform);

  OutBoneMap.Add(Node->Name, BoneName);

  for (const auto &Child : Node->Children) {
    BuildSkeletonHierarchy(Child, Modifier, BoneName, OutBoneMap);
  }
}

UObject *UBVHFactory::FactoryCreateFile(UClass *InClass, UObject *InParent,
                                        FName InName, EObjectFlags Flags,
                                        const FString &Filename,
                                        const TCHAR *Parms,
                                        FFeedbackContext *Warn,
                                        bool &bOutOperationCanceled) {
  UE_LOG(LogTemp, Log, TEXT("BVHFactory: Starting import of %s"), *Filename);

  FBVHParser Parser(Filename);
  FBVHData Data;
  if (!Parser.Parse(Data)) {
    UE_LOG(LogTemp, Error, TEXT("BVHFactory: Failed to parse BVH file."));
    Warn->Log(ELogVerbosity::Error, TEXT("Failed to parse BVH file."));
    return nullptr;
  }

  if (!Data.RootNode.IsValid()) {
    UE_LOG(LogTemp, Error,
           TEXT("BVHFactory: RootNode is invalid after parsing."));
    return nullptr;
  }

  UE_LOG(LogTemp, Log,
         TEXT("BVHFactory: Parsing successful. RootNode: %s, Frames: %d"),
         *Data.RootNode->Name, Data.NumFrames);

  // Flatten nodes early for easier access
  TArray<TSharedPtr<FBVHNode>> FlatNodes;
  struct FNodeCollector {
    static void Collect(TSharedPtr<FBVHNode> Node,
                        TArray<TSharedPtr<FBVHNode>> &OutArray) {
      if (!Node.IsValid())
        return;
      OutArray.Add(Node);
      for (auto Child : Node->Children) {
        Collect(Child, OutArray);
      }
    }
  };
  FNodeCollector::Collect(Data.RootNode, FlatNodes);
  UE_LOG(LogTemp, Log, TEXT("BVHFactory: Flattened nodes. Count: %d"),
         FlatNodes.Num());

  USkeleton *Skeleton = nullptr;
  USkeletalMesh *SkeletalMesh = nullptr;
  bool bSkeletonCreated = false;
  TMap<FString, FName> BoneMap; // BVH Node Name -> UE Bone Name

  // Check for existing Skeleton in the target folder
  FAssetRegistryModule &AssetRegistryModule =
      FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
  TArray<FAssetData> AssetData;

  // Use parent path to search for existing assets in the same folder
  FString TargetFolderPath = FPaths::GetPath(InParent->GetPathName());
  UE_LOG(LogTemp, Log, TEXT("BVHFactory: Searching for Skeleton in path: %s"),
         *TargetFolderPath);
  AssetRegistryModule.Get().GetAssetsByPath(FName(*TargetFolderPath),
                                            AssetData);
  UE_LOG(LogTemp, Log, TEXT("BVHFactory: Found %d assets in path."),
         AssetData.Num());

  for (const FAssetData &Asset : AssetData) {
    UE_LOG(LogTemp, Log, TEXT("BVHFactory: Checking asset: %s, Class: %s"),
           *Asset.AssetName.ToString(), *Asset.AssetClassPath.ToString());

    // Check for Skeleton class (handle both exact match and inheritance if
    // needed, though exact match is usually fine for factories)
    if (Asset.AssetClassPath.GetAssetName() ==
        USkeleton::StaticClass()->GetFName()) {
      Skeleton = Cast<USkeleton>(Asset.GetAsset());
      if (Skeleton) {
        UE_LOG(LogTemp, Log,
               TEXT("BVHFactory: Found existing Skeleton: %s. Reusing it."),
               *Skeleton->GetName());
        break;
      } else {
        UE_LOG(
            LogTemp, Warning,
            TEXT("BVHFactory: Found Skeleton asset but failed to load it: %s"),
            *Asset.AssetName.ToString());
      }
    }
  }

  if (Skeleton) {
    // Reuse existing skeleton
    // Build BoneMap assuming 1:1 mapping
    for (const auto &Node : FlatNodes) {
      BoneMap.Add(Node->Name, FName(*Node->Name));
    }
  } else {
    // 1. Create Skeleton
    UE_LOG(LogTemp, Log, TEXT("BVHFactory: Creating Skeleton..."));
    FString SkeletonName = InName.ToString() + TEXT("_Skeleton");
    FString SkeletonPackageName =
        FPaths::Combine(FPaths::GetPath(InParent->GetPathName()), SkeletonName);
    UPackage *SkeletonPackage = CreatePackage(*SkeletonPackageName);
    Skeleton = NewObject<USkeleton>(SkeletonPackage, FName(*SkeletonName),
                                    Flags | RF_Public | RF_Standalone |
                                        RF_Transactional);
    bSkeletonCreated = true;
  }
  // Build Reference Skeleton locally first
  FReferenceSkeleton LocalRefSkeleton;
  {
    FReferenceSkeletonModifier Modifier(LocalRefSkeleton, nullptr);

    UE_LOG(LogTemp, Log, TEXT("BVHFactory: Building Skeleton Hierarchy..."));
    BuildSkeletonHierarchy(Data.RootNode, Modifier, NAME_None, BoneMap);
    UE_LOG(LogTemp, Log, TEXT("BVHFactory: Hierarchy built. Bone count: %d"),
           BoneMap.Num());
  }

  UE_LOG(LogTemp, Log, TEXT("BVHFactory: LocalRefSkeleton bone count: %d"),
         LocalRefSkeleton.GetNum());
  if (LocalRefSkeleton.GetNum() == 0) {
    UE_LOG(LogTemp, Error, TEXT("BVHFactory: LocalRefSkeleton is empty!"));
    return nullptr;
  }

  // 2. Create Skeletal Mesh (Dummy)
  UE_LOG(LogTemp, Log, TEXT("BVHFactory: Creating Skeletal Mesh..."));
  FString MeshName = InName.ToString() + TEXT("_Mesh");
  FString MeshPackageName =
      FPaths::Combine(FPaths::GetPath(InParent->GetPathName()), MeshName);
  UPackage *MeshPackage = CreatePackage(*MeshPackageName);
  SkeletalMesh = NewObject<USkeletalMesh>(MeshPackage, FName(*MeshName),
                                          Flags | RF_Public | RF_Standalone |
                                              RF_Transactional);

  if (bSkeletonCreated) {
    SkeletalMesh->SetSkeleton(Skeleton);

    SkeletalMesh->PreEditChange(nullptr);

    // Create a dummy triangle to satisfy engine requirements for skeletal
    // meshes
    FSkeletalMeshImportData ImportData;
    ImportData.Points.Add(FVector3f(0, 0, 0));
    ImportData.Points.Add(FVector3f(0, 1, 0));
    ImportData.Points.Add(FVector3f(0, 0, 1));
    SkeletalMeshImportData::FVertex V0, V1, V2;
    V0.VertexIndex = 0;
    V1.VertexIndex = 1;
    V2.VertexIndex = 2;
    V0.MatIndex = 0;
    V1.MatIndex = 0;
    V2.MatIndex = 0;
    V0.UVs[0] = FVector2f(0, 0);
    V1.UVs[0] = FVector2f(1, 0);
    V2.UVs[0] = FVector2f(0, 1);

    ImportData.Wedges.Add(V0);
    ImportData.Wedges.Add(V1);
    ImportData.Wedges.Add(V2);

    SkeletalMeshImportData::FTriangle Tri;
    Tri.WedgeIndex[0] = 0;
    Tri.WedgeIndex[1] = 1;
    Tri.WedgeIndex[2] = 2;
    Tri.MatIndex = 0;
    Tri.AuxMatIndex = 0;
    Tri.SmoothingGroups = 1; // Use 1 for smoothing
    Tri.TangentZ[0] = FVector3f(0, 0, 1);
    Tri.TangentZ[1] = FVector3f(0, 0, 1);
    Tri.TangentZ[2] = FVector3f(0, 0, 1);
    Tri.TangentX[0] = FVector3f(1, 0, 0);
    Tri.TangentX[1] = FVector3f(1, 0, 0);
    Tri.TangentX[2] = FVector3f(1, 0, 0);
    Tri.TangentY[0] = FVector3f(0, 1, 0);
    Tri.TangentY[1] = FVector3f(0, 1, 0);
    Tri.TangentY[2] = FVector3f(0, 1, 0);

    ImportData.Faces.Add(Tri);

    // Add Influences (Bind all to root bone 0)
    for (int32 i = 0; i < 3; ++i) {
      SkeletalMeshImportData::FRawBoneInfluence Influence;
      Influence.VertexIndex = i;
      Influence.BoneIndex = 0;
      Influence.Weight = 1.0f;
      ImportData.Influences.Add(Influence);
      ImportData.PointToRawMap.Add(i);
    }

    SkeletalMeshImportData::FMaterial Mat;
    Mat.MaterialImportName = TEXT("DummyMat");
    ImportData.Materials.Add(Mat);

    // Populate RefBonesBinary from Skeleton
    const TArray<FMeshBoneInfo> &RefBoneInfos =
        LocalRefSkeleton.GetRefBoneInfo();
    const TArray<FTransform> &RefBonePose = LocalRefSkeleton.GetRefBonePose();

    for (int32 i = 0; i < RefBoneInfos.Num(); ++i) {
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
    for (int32 i = 0; i < ImportData.RefBonesBinary.Num(); ++i) {
      int32 ParentIdx = ImportData.RefBonesBinary[i].ParentIndex;
      if (ParentIdx != INDEX_NONE &&
          ParentIdx < ImportData.RefBonesBinary.Num()) {
        ImportData.RefBonesBinary[ParentIdx].NumChildren++;
      }
    }

    // Finalize Skeleton and Mesh
    SkeletalMesh->SetRefSkeleton(LocalRefSkeleton);
    SkeletalMesh->CalculateInvRefMatrices();

    // Sync Skeleton with SkeletalMesh
    if (Skeleton->MergeAllBonesToBoneTree(SkeletalMesh)) {
      UE_LOG(LogTemp, Log,
             TEXT("BVHFactory: Merged bones to Skeleton successfully."));
    } else {
      UE_LOG(LogTemp, Warning,
             TEXT("BVHFactory: MergeAllBonesToBoneTree returned false."));
    }

    FSkeletalMeshLODInfo &LODInfo = SkeletalMesh->AddLODInfo();
    LODInfo.ScreenSize.Default = 1.0f;
    LODInfo.LODHysteresis = 0.02f;
    LODInfo.bAllowCPUAccess = true;

    // Add Default Material
    FSkeletalMaterial MeshMaterial;
    MeshMaterial.MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
    MeshMaterial.MaterialSlotName = TEXT("DummyMat");
    MeshMaterial.ImportedMaterialSlotName = TEXT("DummyMat");
    SkeletalMesh->GetMaterials().Add(MeshMaterial);

    // Ensure ImportedModel has an LODModel for LOD 0
    if (SkeletalMesh->GetImportedModel()) {
      if (SkeletalMesh->GetImportedModel()->LODModels.Num() == 0) {
        SkeletalMesh->GetImportedModel()->LODModels.Add(
            new FSkeletalMeshLODModel());
      }
    } else {
      UE_LOG(LogTemp, Error,
             TEXT("BVHFactory: SkeletalMesh has no ImportedModel!"));
    }

    // Migrate to MeshDescription
    FMeshDescription MeshDescription;
    FSkeletalMeshAttributes MeshAttributes(MeshDescription);
    MeshAttributes.Register();

    // Build Mesh
    IMeshUtilities &MeshUtilities =
        FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(
            "MeshUtilities");

    // Convert ImportData to MeshDescription
    // Use ImportData.GetMeshDescription instead of MeshUtilities
    ImportData.GetMeshDescription(SkeletalMesh, &LODInfo.BuildSettings,
                                  MeshDescription);

    UE_LOG(LogTemp, Log,
           TEXT("BVHFactory: MeshDescription Stats: Vertices=%d, Polygons=%d"),
           MeshDescription.Vertices().Num(), MeshDescription.Polygons().Num());

    // Calculate Bounds
    FBox3f FloatBox(ImportData.Points);
    FBox BoundingBox(FloatBox);
    SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));

    // Commit to SkeletalMesh
    SkeletalMesh->CreateMeshDescription(0, MoveTemp(MeshDescription));
    SkeletalMesh->CommitMeshDescription(0);

    // Explicitly build the LODModel using MeshUtilities to ensure RenderData
    // can be generated
    if (SkeletalMesh->GetImportedModel() &&
        SkeletalMesh->GetImportedModel()->LODModels.Num() > 0) {
      UE_LOG(
          LogTemp, Log,
          TEXT("BVHFactory: ImportData Stats: Points=%d, Wedges=%d, Faces=%d, "
               "Influences=%d"),
          ImportData.Points.Num(), ImportData.Wedges.Num(),
          ImportData.Faces.Num(), ImportData.Influences.Num());

      FSkeletalMeshLODModel &LODModel =
          SkeletalMesh->GetImportedModel()->LODModels[0];
      IMeshUtilities::MeshBuildOptions BuildOptions;
      BuildOptions.FillOptions(LODInfo.BuildSettings);

      // Convert ImportData types to BuildSkeletalMesh types
      TArray<SkeletalMeshImportData::FVertInfluence> Influences;
      Influences.Reserve(ImportData.Influences.Num());
      for (const auto &RawInfluence : ImportData.Influences) {
        SkeletalMeshImportData::FVertInfluence Influence;
        Influence.Weight = RawInfluence.Weight;
        Influence.VertIndex = RawInfluence.VertexIndex;
        Influence.BoneIndex = RawInfluence.BoneIndex;
        Influences.Add(Influence);
      }

      TArray<SkeletalMeshImportData::FMeshWedge> Wedges;
      Wedges.Reserve(ImportData.Wedges.Num());
      for (const auto &RawWedge : ImportData.Wedges) {
        SkeletalMeshImportData::FMeshWedge Wedge;
        Wedge.iVertex = RawWedge.VertexIndex;
        for (int32 i = 0; i < MAX_TEXCOORDS; ++i) {
          Wedge.UVs[i] = RawWedge.UVs[i];
        }
        Wedge.Color = RawWedge.Color;
        Wedges.Add(Wedge);
      }

      TArray<SkeletalMeshImportData::FMeshFace> Faces;
      Faces.Reserve(ImportData.Faces.Num());
      for (const auto &RawFace : ImportData.Faces) {
        SkeletalMeshImportData::FMeshFace Face;
        Face.iWedge[0] = RawFace.WedgeIndex[0];
        Face.iWedge[1] = RawFace.WedgeIndex[1];
        Face.iWedge[2] = RawFace.WedgeIndex[2];
        Face.MeshMaterialIndex = RawFace.MatIndex;
        Face.SmoothingGroups = RawFace.SmoothingGroups;
        for (int32 i = 0; i < 3; ++i) {
          Face.TangentX[i] = RawFace.TangentX[i];
          Face.TangentY[i] = RawFace.TangentY[i];
          Face.TangentZ[i] = RawFace.TangentZ[i];
        }
        Faces.Add(Face);
      }

      bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(
          LODModel, SkeletalMesh->GetName(), SkeletalMesh->GetRefSkeleton(),
          Influences, Wedges, Faces, ImportData.Points,
          ImportData.PointToRawMap, BuildOptions);

      if (bBuildSuccess) {
        UE_LOG(LogTemp, Log, TEXT("BVHFactory: BuildSkeletalMesh successful."));
      } else {
        UE_LOG(LogTemp, Error, TEXT("BVHFactory: BuildSkeletalMesh failed!"));
      }
    }

    if (SkeletalMesh->GetImportedModel() &&
        SkeletalMesh->GetImportedModel()->LODModels.Num() > 0) {
      UE_LOG(LogTemp, Log,
             TEXT("BVHFactory: ImportedModel created successfully. LODModels "
                  "count: %d"),
             SkeletalMesh->GetImportedModel()->LODModels.Num());
    } else {
      UE_LOG(LogTemp, Error,
             TEXT("BVHFactory: ImportedModel is invalid or has no LODModels "
                  "after CommitMeshDescription!"));
    }

    // Ensure compilation is finished
    if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0) {
      FAssetCompilingManager::Get().FinishAllCompilation();
    }

    Skeleton->SetPreviewMesh(SkeletalMesh);

    // Force InitResources
    SkeletalMesh->PostLoad();
    SkeletalMesh->CalculateExtendedBounds();

    Skeleton->PostEditChange();

    if (SkeletalMesh->GetResourceForRendering()) {
      UE_LOG(LogTemp, Log,
             TEXT("BVHFactory: SkeletalMesh has valid RenderData."));
    }

    FAssetRegistryModule::AssetCreated(Skeleton);
    FAssetRegistryModule::AssetCreated(SkeletalMesh);
  }
  // 3. Create AnimSequence
  UE_LOG(LogTemp, Log, TEXT("BVHFactory: Creating AnimSequence..."));
  UAnimSequence *AnimSequence = NewObject<UAnimSequence>(
      InParent, InName, Flags | RF_Public | RF_Standalone | RF_Transactional);
  AnimSequence->SetSkeleton(Skeleton);
  AnimSequence->SetPreviewMesh(SkeletalMesh);

  // Initialize the data model (creates MovieScene etc.)
  AnimSequence->GetController().InitializeModel();

  // Reset NumberOfFrames to 0 to avoid incompatible resampling errors when
  // changing FrameRate InitializeModel() might create a default sequence with
  // non-zero length at default FrameRate (30fps).

  FFrameRate PlatformTargetFrameRate =
      UAnimationSettings::Get()->GetDefaultFrameRate();

  AnimSequence->GetController().SetNumberOfFrames(FFrameNumber(0));
  AnimSequence->GetController().SetFrameRate(PlatformTargetFrameRate);
  AnimSequence->GetController().SetNumberOfFrames(FFrameNumber(Data.NumFrames));

  // Populate Animation Data using AnimationBlueprintLibrary
  // This handles the data model initialization and curve creation more robustly
  TMap<FString, TSharedPtr<FBVHNode>> NodeNameMap;
  int32 CurrentChannelIdx = 0;
  for (auto Node : FlatNodes) {
    Node->ChannelStartIndex = CurrentChannelIdx;
    CurrentChannelIdx += Node->Channels.Num();
    NodeNameMap.Add(Node->Name, Node);
  }

  for (const auto &Pair : BoneMap) {
    const FString &NodeName = Pair.Key;
    const FName &BoneName = Pair.Value;

    if (!NodeNameMap.Contains(NodeName))
      continue;
    const TSharedPtr<FBVHNode> &Node = NodeNameMap[NodeName];

    if (!Node.IsValid())
      continue;

    AnimSequence->GetController().AddBoneCurve(BoneName, true);

    TArray<float> Times;
    TArray<FTransform> Transforms;
    Times.Reserve(Data.NumFrames);
    Transforms.Reserve(Data.NumFrames);

    for (int32 Frame = 0; Frame < Data.NumFrames; ++Frame) {
      const TArray<double> &FrameValues = Data.MotionData[Frame];

      FVector3d LocalPos = Node->Offset;

      bool bHasPos = false;
      FVector3d ChanPos = FVector3d::ZeroVector;

      // Process channels in order
      for (int32 i = 0; i < Node->Channels.Num(); ++i) {
        double Val = FrameValues[Node->ChannelStartIndex + i];
        EBVHChannel Chan = Node->Channels[i];

        switch (Chan) {
        case EBVHChannel::Xposition:
          ChanPos.X = Val;
          bHasPos = true;
          break;
        case EBVHChannel::Yposition:
          ChanPos.Y = Val;
          bHasPos = true;
          break;
        case EBVHChannel::Zposition:
          ChanPos.Z = Val;
          bHasPos = true;
          break;
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
      for (int32 i = 0; i < Node->Channels.Num(); ++i) {
        double Val = FrameValues[Node->ChannelStartIndex + i];
        EBVHChannel Chan = Node->Channels[i];
        FQuat ChanRot = FQuat::Identity;

        if (Chan == EBVHChannel::Xrotation)
          ChanRot = FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(Val));
        else if (Chan == EBVHChannel::Yrotation)
          ChanRot = FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(Val));
        else if (Chan == EBVHChannel::Zrotation)
          ChanRot = FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(Val));

        if (!ChanRot.IsIdentity()) {
          LocalRot = LocalRot * ChanRot;
        }
      }

      if (bHasPos) {
        LocalPos = ChanPos;
      }

      Times.Add(Frame * Data.FrameTime);
      Transforms.Add(FTransform(ConvertRot(LocalRot), ConvertPos(LocalPos),
                                FVector::OneVector));
    }

    TArray<FVector> PositionalKeys;
    TArray<FQuat> RotationalKeys;
    TArray<FVector> ScalingKeys;
    PositionalKeys.Reserve(Transforms.Num());
    RotationalKeys.Reserve(Transforms.Num());
    ScalingKeys.Reserve(Transforms.Num());

    for (const FTransform &Transform : Transforms) {
      PositionalKeys.Add(Transform.GetLocation());
      RotationalKeys.Add(Transform.GetRotation());
      ScalingKeys.Add(Transform.GetScale3D());
    }

    AnimSequence->GetController().SetBoneTrackKeys(
        BoneName, PositionalKeys, RotationalKeys, ScalingKeys, true);
  }

  AnimSequence->GetController().NotifyPopulated();
  AnimSequence->PostEditChange();

  // Notify Asset Registry
  FAssetRegistryModule::AssetCreated(AnimSequence);

  return AnimSequence;
}
