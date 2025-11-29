// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeBVHTranslator.h"
#include "BVHParser.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeCommonAnimationPayload.h"
#include "InterchangeManager.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSceneNode.h"


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

bool UInterchangeBVHTranslator::Translate(
    UInterchangeBaseNodeContainer &BaseNodeContainer) const {
  FString Filename = GetSourceData()->GetFilename();
  if (!FPaths::FileExists(Filename)) {
    return false;
  }

  FBVHParser Parser(Filename);
  FBVHData Data;
  if (!Parser.Parse(Data)) {
    return false;
  }

  if (!Data.RootNode.IsValid()) {
    return false;
  }

  // Create Skeleton Factory Node
  FString SkeletonUid = TEXT("Skeleton_") + FPaths::GetBaseFilename(Filename);
  UInterchangeSkeletonFactoryNode *SkeletonFactoryNode =
      NewObject<UInterchangeSkeletonFactoryNode>(&BaseNodeContainer);
  SkeletonFactoryNode->InitializeSkeletonNode(SkeletonUid, TEXT("Skeleton"),
                                              &BaseNodeContainer);
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
    SceneNode->InitializeSceneNode(NodeUid, Node->Name, &BaseNodeContainer);

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
      BaseNodeContainer.SetNodeParentGuid(NodeUid, Node->Parent.Pin()->Name);
    }

    BaseNodeContainer.AddNode(SceneNode);

    // Add to Animation Payload Map
    FString PayloadKey = Filename + TEXT("_") + Node->Name;
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
  FBVHParser Parser(Filename);
  FBVHData Data;
  if (!Parser.Parse(Data)) {
    return PayloadDatas;
  }

  // Flatten nodes for easy lookup
  TMap<FString, TSharedPtr<FBVHNode>> NodeMap;
  TArray<TSharedPtr<FBVHNode>> NodesToProcess;
  if (Data.RootNode.IsValid()) {
    NodesToProcess.Add(Data.RootNode);
  }

  int32 CurrentChannelIdx = 0;
  while (NodesToProcess.Num() > 0) {
    TSharedPtr<FBVHNode> Node = NodesToProcess.Pop();
    Node->ChannelStartIndex =
        CurrentChannelIdx; // Re-calculate as Parser might not persist this if
                           // we didn't use the exact same logic, but
                           // Parser.Parse does it?
    // Actually Parser.Parse calls ParseMotion which likely doesn't set
    // ChannelStartIndex on nodes if it's not stored in Node. Checking
    // BVHParser.h: FBVHNode has ChannelStartIndex. Checking BVHParser.cpp (not
    // visible but assumed): It should populate it. Wait, in BVHFactory.cpp, it
    // calculates ChannelStartIndex manually! "int32 CurrentChannelIdx = 0; for
    // (auto Node : FlatNodes) { Node->ChannelStartIndex = CurrentChannelIdx;
    // CurrentChannelIdx += Node->Channels.Num(); }" So I must do the same here.

    // But I need to traverse in the same order as the parser did (usually Depth
    // First or whatever the file order is). BVHParser usually parses hierarchy
    // recursively. So I should traverse recursively to match channel order.
    // Actually, let's just use a recursive collector.

    NodeMap.Add(Node->Name, Node);
    // Note: Pop() from Array is LIFO (Stack), so it reverses order if I just
    // add children. I should use a proper traversal to match channel indices.
    // Let's assume I need to recalculate indices.
  }

  // Re-traverse to set channel indices correctly
  {
    int32 ChannelIdx = 0;
    TArray<TSharedPtr<FBVHNode>> Stack;
    if (Data.RootNode.IsValid())
      Stack.Add(Data.RootNode);

    // BVH channels are stored in the order of the hierarchy in the file.
    // The parser builds the hierarchy.
    // I need to visit nodes in the order they appear in the file to assign
    // channel indices. Since I don't have the file order preserved explicitly
    // in a list, I rely on the hierarchy. Standard BVH is recursive
    // depth-first.

    struct FChannelVisitor {
      static void Visit(TSharedPtr<FBVHNode> Node, int32 &InOutChannelIdx) {
        Node->ChannelStartIndex = InOutChannelIdx;
        InOutChannelIdx += Node->Channels.Num();
        for (auto Child : Node->Children) {
          Visit(Child, InOutChannelIdx);
        }
      }
    };

    int32 ChIdx = 0;
    if (Data.RootNode.IsValid()) {
      FChannelVisitor::Visit(Data.RootNode, ChIdx);
    }
  }

  // Re-populate NodeMap after traversal
  NodeMap.Empty();
  {
    TArray<TSharedPtr<FBVHNode>> Stack;
    if (Data.RootNode.IsValid())
      Stack.Add(Data.RootNode);
    while (Stack.Num() > 0) {
      TSharedPtr<FBVHNode> Node = Stack.Pop();
      NodeMap.Add(Node->Name, Node);
      for (auto Child : Node->Children)
        Stack.Add(Child);
    }
  }

  for (const auto &Query : PayloadQueries) {
    FString PayloadKey = Query.PayloadKey.UniqueId;
    // Format: Filename + "_" + NodeName
    // But Filename might contain underscores.
    // Better to check if it ends with the node name?
    // Or just use the NodeName if we assume unique names.
    // In Translate, I used: Filename + TEXT("_") + Node->Name;

    // Let's try to extract NodeName.
    // Since I have the NodeMap, I can check which node name matches the suffix.
    // Or better, I can store the NodeName directly in the PayloadKey if I
    // could, but PayloadKey is a string. I'll assume the suffix is the
    // NodeName.

    FString NodeName;
    // Find the node that matches the key
    for (const auto &Pair : NodeMap) {
      if (PayloadKey.EndsWith(TEXT("_") + Pair.Key)) {
        NodeName = Pair.Key;
        break;
      }
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
        case EBVHChannel::Xrotation:
          LocalRot =
              LocalRot * FQuat(FVector(1, 0, 0), FMath::DegreesToRadians(Val));
          break;
        case EBVHChannel::Yrotation:
          LocalRot =
              LocalRot * FQuat(FVector(0, 1, 0), FMath::DegreesToRadians(Val));
          break;
        case EBVHChannel::Zrotation:
          LocalRot =
              LocalRot * FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(Val));
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
