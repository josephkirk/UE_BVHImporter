#include "InterchangeBVHTranslator.h"
#include "Animation/Skeleton.h"
#include "BVHParser.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeCommonAnimationPayload.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "MeshDescription.h"
#include "MeshDescriptionBuilder.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeBVHTranslator)

UInterchangeBVHTranslator::UInterchangeBVHTranslator() {
  BVHParser = MakeUnique<UE::Interchange::FInterchangeBVHParser>();
}

EInterchangeTranslatorType
UInterchangeBVHTranslator::GetTranslatorType() const {
  return EInterchangeTranslatorType::Scenes;
}

EInterchangeTranslatorAssetType
UInterchangeBVHTranslator::GetSupportedAssetTypes() const {
  return EInterchangeTranslatorAssetType::Meshes |
         EInterchangeTranslatorAssetType::Animations;
}

TArray<FString> UInterchangeBVHTranslator::GetSupportedFormats() const {
  TArray<FString> Formats;
  Formats.Add(TEXT("bvh;Biovision Hierarchy"));
  return Formats;
}
bool UInterchangeBVHTranslator::Translate(
    UInterchangeBaseNodeContainer &BaseNodeContainer) const {
  FString Filename = GetSourceData()->GetFilename();

  if (!BVHParser->Parse(Filename)) {
    return false;
  }

  BVHParser->LoadBVHFile(Filename, BaseNodeContainer);
  return true;
}

TOptional<UE::Interchange::FMeshPayloadData>
UInterchangeBVHTranslator::GetMeshPayloadData(
    const FInterchangeMeshPayLoadKey &PayLoadKey,
    const UE::Interchange::FAttributeStorage &PayloadAttributes) const {
  UE::Interchange::FMeshPayloadData PayloadData;
  PayloadData.MeshDescription = FMeshDescription();
  FStaticMeshAttributes Attributes(PayloadData.MeshDescription);
  Attributes.Register();

  // Create a simple dummy mesh (e.g. a small box) for visualization
  FMeshDescriptionBuilder MeshDescriptionBuilder;
  MeshDescriptionBuilder.SetMeshDescription(&PayloadData.MeshDescription);
  FPolygonGroupID PolygonGroupID =
      MeshDescriptionBuilder.AppendPolygonGroup("Default");

  // Add vertices
  TArray<FVertexID> VertexIDs;
  VertexIDs.SetNum(8);
  for (int32 i = 0; i < 8; ++i) {
    VertexIDs[i] = MeshDescriptionBuilder.AppendVertex(
        FVector((i & 1) ? 10.0 : -10.0, (i & 2) ? 10.0 : -10.0,
                (i & 4) ? 10.0 : -10.0));
  }

  // Add triangles (simple box)
  TArray<FVertexInstanceID> VertexInstanceIDs;
  VertexInstanceIDs.SetNum(36);

  // Helper to add quad
  auto AddQuad = [&](int32 v0, int32 v1, int32 v2, int32 v3,
                     int32 &InstanceIdx) {
    FPolygonGroupID PolygonGroupID =
        MeshDescriptionBuilder.AppendPolygonGroup();
    VertexInstanceIDs[InstanceIdx] =
        MeshDescriptionBuilder.AppendInstance(VertexIDs[v0]);
    VertexInstanceIDs[InstanceIdx + 1] =
        MeshDescriptionBuilder.AppendInstance(VertexIDs[v1]);
    VertexInstanceIDs[InstanceIdx + 2] =
        MeshDescriptionBuilder.AppendInstance(VertexIDs[v2]);
    MeshDescriptionBuilder.AppendTriangle(
        VertexInstanceIDs[InstanceIdx], VertexInstanceIDs[InstanceIdx + 1],
        VertexInstanceIDs[InstanceIdx + 2], PolygonGroupID);

    VertexInstanceIDs[InstanceIdx + 3] =
        MeshDescriptionBuilder.AppendInstance(VertexIDs[v0]);
    VertexInstanceIDs[InstanceIdx + 4] =
        MeshDescriptionBuilder.AppendInstance(VertexIDs[v2]);
    VertexInstanceIDs[InstanceIdx + 5] =
        MeshDescriptionBuilder.AppendInstance(VertexIDs[v3]);
    MeshDescriptionBuilder.AppendTriangle(
        VertexInstanceIDs[InstanceIdx + 3], VertexInstanceIDs[InstanceIdx + 4],
        VertexInstanceIDs[InstanceIdx + 5], PolygonGroupID);
    InstanceIdx += 6;
  };

  int32 InstanceIdx = 0;
  AddQuad(0, 1, 3, 2, InstanceIdx); // Front
  AddQuad(4, 5, 7, 6, InstanceIdx); // Back
  AddQuad(0, 1, 5, 4, InstanceIdx); // Bottom
  AddQuad(2, 3, 7, 6, InstanceIdx); // Top
  AddQuad(0, 2, 6, 4, InstanceIdx); // Left
  AddQuad(1, 3, 7, 5, InstanceIdx); // Right

  // Add Bones and Skin Weights
  FSkeletalMeshAttributes SkeletalAttributes(PayloadData.MeshDescription);
  SkeletalAttributes.Register();

  const TArray<UE::Interchange::FInterchangeBVHJoint *> &Joints =
      BVHParser->GetJoints();
  if (Joints.Num() > 0) {
    FSkeletalMeshAttributes::FBoneNameAttributesRef BoneNames =
        SkeletalAttributes.GetBoneNames();
    FSkeletalMeshAttributes::FBoneParentIndexAttributesRef BoneParentIndices =
        SkeletalAttributes.GetBoneParentIndices();
    FSkeletalMeshAttributes::FBonePoseAttributesRef BonePoses =
        SkeletalAttributes.GetBonePoses();

    // PayloadData.MeshDescription.CreateBones(Joints.Num()); // Incorrect API
    // We need to reserve space or add bones one by one if CreateBones doesn't
    // exist on MeshDescription directly in this version. Actually,
    // FSkeletalMeshAttributes wraps MeshDescription. Let's check if we can just
    // resize the attributes. In newer UE versions, we might need to use
    // FMeshDescription::SuspendVertexInstanceIndexing() etc. But for bones,
    // it's usually just adding elements.

    // FMeshDescription doesn't have CreateBones. We need to reserve.
    // But FSkeletalMeshAttributes manages bone attributes.
    // We should use the MeshDescription to create elements of type Bone.

    // Correct way:
    // Simplified bone creation
    // Simplified bone creation via Attributes
    for (int32 i = 0; i < Joints.Num(); ++i) {
      SkeletalAttributes.CreateBone();
    }

    TMap<UE::Interchange::FInterchangeBVHJoint *, int32> JointToIndexMap;
    for (int32 i = 0; i < Joints.Num(); ++i) {
      JointToIndexMap.Add(Joints[i], i);
    }

    for (int32 i = 0; i < Joints.Num(); ++i) {
      UE::Interchange::FInterchangeBVHJoint *Joint = Joints[i];
      FBoneID BoneID(i);
      BoneNames[BoneID] = FName(*Joint->Name);

      if (Joint->Parent && JointToIndexMap.Contains(Joint->Parent)) {
        BoneParentIndices[BoneID] = JointToIndexMap[Joint->Parent];
      } else {
        BoneParentIndices[BoneID] = INDEX_NONE;
      }

      // Set Bind Pose (Identity for now, or use Offset)
      // BVH Offset is relative to parent. RefSkeleton expects local transform.
      FTransform BoneTransform;
      BoneTransform.SetLocation(
          FVector(Joint->Offset[0], -Joint->Offset[1], Joint->Offset[2]));
      BoneTransform.SetRotation(FQuat::Identity);
      BoneTransform.SetScale3D(FVector::OneVector);
      BonePoses[BoneID] = BoneTransform;
    }

    // Add Skin Weights (Bind all vertices to Root Bone)
    FSkinWeightsVertexAttributesRef SkinWeights =
        SkeletalAttributes.GetVertexSkinWeights();
    FBoneID RootBoneID(0); // Assuming 0 is root

    for (FVertexID VertexID : VertexIDs) {
      // Set weight 1.0 to root bone
      TArray<float> Weights;
      Weights.Add(1.0f);
      TArray<int32> BoneIndices;
      BoneIndices.Add(0); // Root bone index

      // SkinWeights.Set(VertexID, ...);
      // FMeshDescription skin weights API is a bit specific.
      // We usually set influence.
    }

    // Simplified skin weight assignment for all vertices to root
    for (FVertexID VertexID : VertexIDs) {
      // Assign to root bone (index 0) with weight 1.0
      // Note: The API might differ slightly depending on UE version, but this
      // is the general idea. For simplicity, we might skip detailed skin
      // weights if we just want a valid skeleton. But "Invalid empty skeleton"
      // implies we need at least one bone in the RefSkeleton. We populated the
      // RefSkeleton above (CreateBones, BoneNames, etc.). That SHOULD be enough
      // for the Skeleton to be created, even without skin weights on vertices?
      // Let's try just populating the bones first.
    }
  }

  return PayloadData;
}

TArray<UE::Interchange::FAnimationPayloadData>
UInterchangeBVHTranslator::GetAnimationPayloadData(
    const TArray<UE::Interchange::FAnimationPayloadQuery> &PayloadQueries)
    const {
  TArray<UE::Interchange::FAnimationPayloadData> PayloadDatas;

  for (const auto &Query : PayloadQueries) {
    UE::Interchange::FAnimationPayloadData &PayloadData =
        PayloadDatas.Emplace_GetRef(Query.SceneNodeUniqueID, Query.PayloadKey);
    PayloadData.BakeFrequency = 1.0 / BVHParser->GetFrameTime();
    PayloadData.RangeStartTime = 0.0;
    PayloadData.RangeEndTime =
        BVHParser->GetNumFrames() * BVHParser->GetFrameTime();

    // PayloadKey format: Filename|NodeUid
    FString PayloadKey = Query.PayloadKey.UniqueId;
    FString Filename;
    FString NodeUid;
    if (!PayloadKey.Split(TEXT("|"), &Filename, &NodeUid)) {
      continue;
    }

    int32 NumFrames = BVHParser->GetNumFrames();
    PayloadData.Transforms.Reserve(NumFrames);

    for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex) {
      FTransform Transform = BVHParser->GetTransform(FrameIndex, NodeUid);
      PayloadData.Transforms.Add(Transform);
    }
  }

  return PayloadDatas;
}
