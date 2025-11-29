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
  SkeletonFactoryNode->InitializeSkeletonNode(
      SkeletonUid, TEXT("Skeleton"), TEXT("Skeleton"), &BaseNodeContainer);
  SkeletonFactoryNode->SetCustomRootJointUid(Data.RootNode->Name);
  BaseNodeContainer.AddNode(SkeletonFactoryNode);

  // Create AnimSequence Factory Node
  FString AnimSequenceUid =
      TEXT("AnimSequence_") + FPaths::GetBaseFilename(Filename);
  UInterchangeAnimSequenceFactoryNode *AnimSequenceFactoryNode =
      NewObject<UInterchangeAnimSequenceFactoryNode>(&BaseNodeContainer);
  AnimSequenceFactoryNode->InitializeAnimSequenceNode(
      AnimSequenceUid, TEXT("AnimSequence"), &BaseNodeContainer);
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
    FString NodeUid = Node->Name;
    UInterchangeSceneNode *SceneNode =
        NewObject<UInterchangeSceneNode>(&BaseNodeContainer);
    SceneNode->InitializeNode(NodeUid, Node->Name,
                              EInterchangeNodeContainerType::TranslatedScene);

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
      BaseNodeContainer.SetNodeParentUid(NodeUid, Node->Parent.Pin()->Name);
    }

    BaseNodeContainer.AddNode(SceneNode);

    // Add to Animation Payload Map
    FString PayloadKey = Filename + TEXT("|") + Node->Name;
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
  TArray<TSharedPtr<FBVHNode>> NodesToProcess;
  if (Data.RootNode.IsValid()) {
    NodesToProcess.Add(Data.RootNode);
  }

  // Traverse hierarchy to set channel indices and populate NodeMap
  {
    int32 ChannelIdx = 0;
    // BVH channels are stored in the order of the hierarchy in the file.
    // The parser builds the hierarchy.
    // Visit nodes in the order they appear in the file to assign channel indices and populate NodeMap.
    struct FChannelVisitor {
      static void Visit(TSharedPtr<FBVHNode> Node, int32 &InOutChannelIdx, TMap<FString, TSharedPtr<FBVHNode>>& NodeMap) {
        Node->ChannelStartIndex = InOutChannelIdx;
        InOutChannelIdx += Node->Channels.Num();
        NodeMap.Add(Node->Name, Node);
        for (auto Child : Node->Children) {
          Visit(Child, InOutChannelIdx, NodeMap);
        }
      }
    };

    int32 ChIdx = 0;
    if (Data.RootNode.IsValid()) {
      FChannelVisitor::Visit(Data.RootNode, ChIdx, NodeMap);
    }
  }
  for (const auto &Query : PayloadQueries) {
    FString PayloadKey = Query.PayloadKey.UniqueId;
    // Format: Filename + "|" + NodeName
    // Using '|' as a delimiter to avoid ambiguity with underscores in filenames
    // or node names.

    // Extract NodeName from PayloadKey using the delimiter.
    FString NodeName;
    int32 DelimIdx;
    if (PayloadKey.FindLastChar(TEXT('|'), DelimIdx)) {
      NodeName = PayloadKey.Mid(DelimIdx + 1);
    }

    if (NodeName.IsEmpty() || !NodeMap.Contains(NodeName)) {
      continue;
    }

    TSharedPtr<FBVHNode> Node = NodeMap[NodeName];
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
          // UE_LOG(LogTemp, Warning, TEXT("ChannelIndex %d out of bounds for FrameValues.Num() %d"), ChannelIndex, FrameValues.Num());
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
          LocalRot = LocalRot * FQuat(FVector::ForwardVector,
                                      FMath::DegreesToRadians(Val));
          break;
        case EBVHChannel::Yrotation:
          LocalRot = LocalRot *
                     FQuat(FVector::RightVector, FMath::DegreesToRadians(Val));
          break;
        case EBVHChannel::Zrotation:
          LocalRot =
              LocalRot * FQuat(FVector::UpVector, FMath::DegreesToRadians(Val));
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
