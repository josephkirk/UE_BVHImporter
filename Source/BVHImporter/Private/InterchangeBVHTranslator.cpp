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
