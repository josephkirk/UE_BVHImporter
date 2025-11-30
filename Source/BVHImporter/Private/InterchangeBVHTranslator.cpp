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
  return EInterchangeTranslatorType::Assets;
}

EInterchangeTranslatorAssetType
UInterchangeBVHTranslator::GetSupportedAssetTypes() const {
  return EInterchangeTranslatorAssetType::Meshes |
         EInterchangeTranslatorAssetType::Animations;
}

TArray<FString> UInterchangeBVHTranslator::GetSupportedFormats() const {
  return {TEXT("bvh;Biovision Hierarchy")};
}

const FBVHData *
UInterchangeBVHTranslator::GetBVHData(const FString &Filename) const {
  if (CachedBVHData.IsSet() && CachedFilename == Filename) {
    return &CachedBVHData.GetValue();
  }

  FBVHParser Parser(Filename);
  FBVHData Data;
  if (Parser.Parse(Data)) {
    CachedBVHData = MoveTemp(Data);
    CachedFilename = Filename;
    return &CachedBVHData.GetValue();
  }

  return nullptr;
}

bool UInterchangeBVHTranslator::Translate(
    UInterchangeBaseNodeContainer &BaseNodeContainer) const {
  FString Filename = GetSourceData()->GetFilename();
  if (!FPaths::FileExists(Filename)) {
    return false;
  }

  if (BVHParser.IsValid()) {
    BVHParser->LoadBVHFile(Filename, BaseNodeContainer);
    return true;
  }

  return false;
}

void VisitNodesRecursive(TSharedPtr<FBVHNode> Node, int32 &InOutChannelIdx,
                         TMap<FString, TSharedPtr<FBVHNode>> &NodeMap) {
  Node->ChannelStartIndex = InOutChannelIdx;
  InOutChannelIdx += Node->Channels.Num();

  // Reconstruct NodeUid
  FString NodeUid = FString::Printf(TEXT("%s_%p"), *Node->Name, Node.Get());
  NodeMap.Add(NodeUid, Node);

  for (auto Child : Node->Children) {
    VisitNodesRecursive(Child, InOutChannelIdx, NodeMap);
  }
}

TArray<UE::Interchange::FAnimationPayloadData>
UInterchangeBVHTranslator::GetAnimationPayloadData(
    const TArray<UE::Interchange::FAnimationPayloadQuery> &PayloadQueries)
    const {
  TArray<UE::Interchange::FAnimationPayloadData> PayloadDatas;

  FString Filename = GetSourceData()->GetFilename();
  const FBVHData *DataPtr = GetBVHData(Filename);
  if (!DataPtr) {
    return PayloadDatas;
  }
  const FBVHData &Data = *DataPtr;

  // Flatten nodes for easy lookup and assign channel indices in one traversal
  TMap<FString, TSharedPtr<FBVHNode>> NodeMap;

  // Traverse hierarchy to set channel indices and populate NodeMap
  {
    int32 ChIdx = 0;
    if (Data.RootNode.IsValid()) {
      VisitNodesRecursive(Data.RootNode, ChIdx, NodeMap);
    }
  }
  for (const auto &Query : PayloadQueries) {
    FString PayloadKey = Query.PayloadKey.UniqueId;
    // Format: Filename + "|" + NodeUid
    // Using '|' as a delimiter to avoid ambiguity with underscores in
    // filenames or node names.

    // Extract NodeUid from PayloadKey using the delimiter.
    FString NodeUid;
    int32 DelimIdx;
    if (PayloadKey.FindLastChar(TEXT('|'), DelimIdx)) {
      NodeUid = PayloadKey.Mid(DelimIdx + 1);
    }

    if (NodeUid.IsEmpty()) {
      continue;
    }

    // We need to find the node that corresponds to this NodeUid.
    // Since NodeUid contains the pointer address, we can't just look up by
    // name. We need to reconstruct the map of NodeUid -> Node. This is done
    // below in the traversal.

    if (!NodeMap.Contains(NodeUid)) {
      continue;
    }

    TSharedPtr<FBVHNode> Node = NodeMap[NodeUid];
    UE::Interchange::FAnimationPayloadData PayloadData(Query.SceneNodeUniqueID,
                                                       Query.PayloadKey);

    PayloadData.BakeFrequency = 1.0 / Data.FrameTime;
    PayloadData.RangeStartTime = 0.0;
    PayloadData.RangeEndTime = Data.NumFrames * Data.FrameTime;

    PayloadData.Transforms.Reserve(Data.NumFrames);

    for (int32 Frame = 0; Frame < Data.NumFrames; ++Frame) {
      const TArray<double> &FrameValues = Data.MotionData[Frame];

      FVector3d LocalPos = Node->Offset;
      FQuat LocalRot = FQuat::Identity;

      bool bHasPos = false;
      FVector3d ChanPos = FVector3d::ZeroVector;

      for (int32 i = 0; i < Node->Channels.Num(); ++i) {
        int32 ChannelIndex = Node->ChannelStartIndex + i;
        if (ChannelIndex >= FrameValues.Num()) {
          // Optionally log an error here, e.g.:
          // UE_LOG(LogTemp, Warning, TEXT("ChannelIndex %d out of bounds for
          // FrameValues.Num() %d"), ChannelIndex, FrameValues.Num());
          continue;
        }
        double Val = FrameValues[ChannelIndex];
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
        case EBVHChannel::Xrotation:
          // BVH X axis -> UE X axis
          LocalRot =
              LocalRot * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(Val));
          break;
        case EBVHChannel::Yrotation:
          // BVH Y axis -> UE Z axis
          LocalRot =
              LocalRot * FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(Val));
          break;
        case EBVHChannel::Zrotation:
          // BVH Z axis -> UE -Y axis
          LocalRot =
              LocalRot * FQuat(FVector(0, -1, 0), FMath::DegreesToRadians(Val));
          break;
        default:
          break;
        }
      }

      if (bHasPos) {
        LocalPos = ChanPos;
      }

      // Convert to UE Coordinates
      // UE_X = BVH_X, UE_Y = -BVH_Z, UE_Z = BVH_Y
      FVector UEPos(LocalPos.X, -LocalPos.Z, LocalPos.Y);
      FQuat UERot(LocalRot.X, -LocalRot.Z, LocalRot.Y, LocalRot.W);

      PayloadData.Transforms.Add(FTransform(UERot, UEPos, FVector::OneVector));
    }

    PayloadDatas.Add(PayloadData);
  }

  return PayloadDatas;
}

TOptional<UE::Interchange::FMeshPayloadData>
UInterchangeBVHTranslator::GetMeshPayloadData(
    const FInterchangeMeshPayLoadKey &PayLoadKey,
    const UE::Interchange::FAttributeStorage &PayloadAttributes) const {
  // We only support one mesh payload, which is the dummy mesh for the
  // skeleton The key should match what we set in the LodDataNode (which we
  // haven't set yet, but we will) Actually, the factory might ask for the
  // mesh payload using the scene node UID if we didn't set a specific payload
  // key in the LodDataNode? Wait, in CreatePayloadTasks, it uses the
  // LodDataNode to get the mesh payload. We need to set a payload key in the
  // LodDataNode.

  UE::Interchange::FMeshPayloadData MeshPayloadData;
  MeshPayloadData.MeshDescription = FMeshDescription();
  FStaticMeshAttributes Attributes(MeshPayloadData.MeshDescription);
  Attributes.Register();

  // Create a single dummy triangle so the mesh is valid
  FMeshDescriptionBuilder Builder;
  Builder.SetMeshDescription(&MeshPayloadData.MeshDescription);
  Builder.EnablePolyGroups();
  Builder.SetNumUVLayers(1);

  TArray<FVertexID> VertexIDs;
  VertexIDs.Add(Builder.AppendVertex(FVector(0, 0, 0)));
  VertexIDs.Add(Builder.AppendVertex(FVector(10, 0, 0)));
  VertexIDs.Add(Builder.AppendVertex(FVector(0, 10, 0)));

  TArray<FVertexInstanceID> VertexInstanceIDs;
  VertexInstanceIDs.Add(Builder.AppendInstance(VertexIDs[0]));
  VertexInstanceIDs.Add(Builder.AppendInstance(VertexIDs[1]));
  VertexInstanceIDs.Add(Builder.AppendInstance(VertexIDs[2]));

  FPolygonGroupID PolygonGroupID = Builder.AppendPolygonGroup();
  Builder.AppendTriangle(VertexInstanceIDs[0], VertexInstanceIDs[1],
                         VertexInstanceIDs[2], PolygonGroupID);

  return MeshPayloadData;
}
