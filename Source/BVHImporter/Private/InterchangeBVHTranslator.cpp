// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeBVHTranslator.h"
#include "BVHParser.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeCommonAnimationPayload.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeBVHTranslator)

EInterchangeTranslatorType
UInterchangeBVHTranslator::GetTranslatorType() const {
  return EInterchangeTranslatorType::Assets;
}

EInterchangeTranslatorAssetType
UInterchangeBVHTranslator::GetSupportedAssetTypes() const {
  return EInterchangeTranslatorAssetType::Animations;
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

  const FBVHData *DataPtr = GetBVHData(Filename);
  if (!DataPtr || !DataPtr->RootNode.IsValid()) {
    return false;
  }
  const FBVHData &Data = *DataPtr;

  // Create Skeleton Factory Node
  FString SkeletonUid = TEXT("Skeleton_") + FPaths::GetBaseFilename(Filename);
  UInterchangeSkeletonFactoryNode *SkeletonFactoryNode =
      NewObject<UInterchangeSkeletonFactoryNode>(&BaseNodeContainer);
  FString SkeletonDisplayName =
      FPaths::GetBaseFilename(Filename) + TEXT("_Skeleton");
  SkeletonFactoryNode->InitializeSkeletonNode(SkeletonUid, SkeletonDisplayName,
                                              SkeletonDisplayName,
                                              &BaseNodeContainer);
  SkeletonFactoryNode->SetCustomRootJointUid(Data.RootNode->Name);
  BaseNodeContainer.AddNode(SkeletonFactoryNode);
  // Create AnimSequence Factory Node
  FString AnimSequenceUid =
      TEXT("AnimSequence_") + FPaths::GetBaseFilename(Filename);
  UInterchangeAnimSequenceFactoryNode *AnimSequenceFactoryNode =
      NewObject<UInterchangeAnimSequenceFactoryNode>(&BaseNodeContainer);
  AnimSequenceFactoryNode->InitializeAnimSequenceNode(
      AnimSequenceUid,
      *FString::Printf(TEXT("%s_Anim"), *FPaths::GetBaseFilename(Filename)),
      &BaseNodeContainer);
  AnimSequenceFactoryNode->SetCustomSkeletonFactoryNodeUid(SkeletonUid);
  BaseNodeContainer.AddNode(AnimSequenceFactoryNode);

  // Process Hierarchy
  TArray<TSharedPtr<FBVHNode>> NodesToProcess;
  NodesToProcess.Add(Data.RootNode);

  TMap<FString, FString> SceneNodeAnimationPayloadKeyUids;
  TMap<FString, uint8> SceneNodeAnimationPayloadKeyTypes;

  while (NodesToProcess.Num() > 0) {
    TSharedPtr<FBVHNode> Node = NodesToProcess.Pop();

    // Create Scene Node
    // Use pointer address to ensure unique UID in case of duplicate names
    FString NodeUid = FString::Printf(TEXT("%s_%p"), *Node->Name, Node.Get());
    UInterchangeSceneNode *SceneNode =
        NewObject<UInterchangeSceneNode>(&BaseNodeContainer);
    SceneNode->InitializeNode(NodeUid, Node->Name,
                              EInterchangeNodeContainerType::TranslatedScene);
    SceneNode->AddSpecializedType(
        UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());

    // Set Transform (Local Offset)
    // BVH Offset is X, Y, Z. Convert to UE: X, -Z, Y
    FTransform LocalTransform;
    LocalTransform.SetLocation(
        FVector(Node->Offset.X, -Node->Offset.Z, Node->Offset.Y));
    LocalTransform.SetRotation(FQuat::Identity);
    LocalTransform.SetScale3D(FVector::OneVector);
    SceneNode->SetCustomLocalTransform(&BaseNodeContainer, LocalTransform);

    // Set Hierarchy
    if (Node->Parent.IsValid()) {
      TSharedPtr<FBVHNode> ParentNode = Node->Parent.Pin();
      FString ParentUid =
          FString::Printf(TEXT("%s_%p"), *ParentNode->Name, ParentNode.Get());
      BaseNodeContainer.SetNodeParentUid(NodeUid, ParentUid);
    }

    BaseNodeContainer.AddNode(SceneNode);

    // Add to Animation Payload Map
    // Payload Key must also be unique or deterministic.
    // Since we use the same cached data, we can use the same logic.
    FString PayloadKey = Filename + TEXT("|") + NodeUid;
    SceneNodeAnimationPayloadKeyUids.Add(NodeUid, PayloadKey);
    SceneNodeAnimationPayloadKeyTypes.Add(
        NodeUid, (uint8)EInterchangeAnimationPayLoadType::BAKED);

    // Add children
    for (const auto &Child : Node->Children) {
      NodesToProcess.Add(Child);
    }
  }

  AnimSequenceFactoryNode->SetAnimationPayloadKeysForSceneNodeUids(
      SceneNodeAnimationPayloadKeyUids, SceneNodeAnimationPayloadKeyTypes);

  return true;
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
    // Using '|' as a delimiter to avoid ambiguity with underscores in filenames
    // or node names.

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
