#include "InterchangeBVHParser.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "InterchangeAnimSequenceFactoryNode.h"
#include "InterchangeAnimationTrackSetNode.h" // Added for EInterchangeAnimationPayLoadType
#include "InterchangeCommonAnimationPayload.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include <fstream>
#include <vector>

namespace UE {
namespace Interchange {

FInterchangeBVHParser::FInterchangeBVHParser() {}
FInterchangeBVHParser::~FInterchangeBVHParser() { Clear(); }

void FInterchangeBVHParser::Clear() {
  for (FInterchangeBVHChannel *Channel : Channels) {
    delete Channel;
  }
  Channels.Empty();

  for (FInterchangeBVHJoint *Joint : Joints) {
    delete Joint;
  }
  Joints.Empty();
  JointMap.Empty();
  NodeUidToJointMap.Empty();

  MotionData.Empty();
  NumFrames = 0;
  FrameTime = 0.0;
}

bool FInterchangeBVHParser::Parse(const FString &Filename) {
  Clear();
  UE_LOG(LogTemp, Log, TEXT("Parsing BVH file: %s"), *Filename);

  if (!FPaths::FileExists(Filename)) {
    UE_LOG(LogTemp, Error, TEXT("BVH file does not exist: %s"), *Filename);
    return false;
  }

  FString FileContent;
  if (!FFileHelper::LoadFileToString(FileContent, *Filename)) {
    UE_LOG(LogTemp, Error, TEXT("Failed to load BVH file content: %s"),
           *Filename);
    return false;
  }

  // Normalize whitespace to spaces
  FileContent.ReplaceInline(TEXT("\r"), TEXT(" "));
  FileContent.ReplaceInline(TEXT("\n"), TEXT(" "));
  FileContent.ReplaceInline(TEXT("\t"), TEXT(" "));

  TArray<FString> Tokens;
  FileContent.ParseIntoArray(Tokens, TEXT(" "), true);

  int32 TokenIndex = 0;
  auto GetNextToken = [&]() -> FString {
    if (TokenIndex < Tokens.Num()) {
      return Tokens[TokenIndex++];
    }
    return FString();
  };

  auto PeekNextToken = [&]() -> FString {
    if (TokenIndex < Tokens.Num()) {
      return Tokens[TokenIndex];
    }
    return FString();
  };

  UE_LOG(LogTemp, Log, TEXT("Total tokens found: %d"), Tokens.Num());

  int32 HierarchyIndex = -1;
  for (int32 i = 0; i < Tokens.Num(); ++i) {
    if (Tokens[i].Contains(TEXT("HIERARCHY"))) {
      HierarchyIndex = i;
      break;
    }
  }

  if (HierarchyIndex == -1) {
    if (Tokens.Num() > 0) {
      UE_LOG(
          LogTemp, Error,
          TEXT("Invalid BVH file: Missing HIERARCHY header. First token: '%s'"),
          *Tokens[0]);
    } else {
      UE_LOG(LogTemp, Error, TEXT("Invalid BVH file: No tokens found"));
    }
    return false;
  }

  UE_LOG(LogTemp, Log, TEXT("Found HIERARCHY at token index: %d"),
         HierarchyIndex);
  TokenIndex = HierarchyIndex + 1;

  TArray<FInterchangeBVHJoint *> JointStack;
  FInterchangeBVHJoint *CurrentJoint = nullptr;
  bool bIsSite = false;
  FString Token;

  while (TokenIndex < Tokens.Num()) {
    Token = GetNextToken();

    if (Token == TEXT("ROOT") || Token == TEXT("JOINT")) {
      FInterchangeBVHJoint *NewJoint = new FInterchangeBVHJoint();
      NewJoint->Index = Joints.Num();
      NewJoint->Parent = CurrentJoint;
      NewJoint->Name = GetNextToken();

      Joints.Add(NewJoint);
      JointMap.Add(NewJoint->Name, NewJoint);

      if (CurrentJoint) {
        CurrentJoint->Children.Add(NewJoint);
      }

      CurrentJoint = NewJoint;
      bIsSite = false;               // Reset site flag for new joint
    } else if (Token == TEXT("End")) // End Site
    {
      Token = GetNextToken(); // Site
      bIsSite = true;
    } else if (Token == TEXT("{")) {
      if (!bIsSite) {
        JointStack.Push(CurrentJoint);
      }
    } else if (Token == TEXT("}")) {
      if (bIsSite) {
        bIsSite = false;
      } else {
        if (JointStack.Num() > 0) {
          CurrentJoint = JointStack.Pop();
        }
      }
    } else if (Token == TEXT("OFFSET")) {
      double X = FCString::Atod(*GetNextToken());
      double Y = FCString::Atod(*GetNextToken());
      double Z = FCString::Atod(*GetNextToken());

      if (bIsSite && CurrentJoint) {
        CurrentJoint->bHasSite = true;
        CurrentJoint->Site[0] = X;
        CurrentJoint->Site[1] = Y;
        CurrentJoint->Site[2] = Z;
      } else if (CurrentJoint) {
        CurrentJoint->Offset[0] = X;
        CurrentJoint->Offset[1] = Y;
        CurrentJoint->Offset[2] = Z;
      }
    } else if (Token == TEXT("CHANNELS")) {
      int32 NumChannels = FCString::Atoi(*GetNextToken());
      for (int32 i = 0; i < NumChannels; ++i) {
        FString ChannelType = GetNextToken();
        FInterchangeBVHChannel *Channel = new FInterchangeBVHChannel();
        Channel->Joint = CurrentJoint;
        Channel->Index = Channels.Num();

        if (ChannelType == TEXT("Xposition"))
          Channel->Type = EInterchangeBVHChannelEnum::X_POSITION;
        else if (ChannelType == TEXT("Yposition"))
          Channel->Type = EInterchangeBVHChannelEnum::Y_POSITION;
        else if (ChannelType == TEXT("Zposition"))
          Channel->Type = EInterchangeBVHChannelEnum::Z_POSITION;
        else if (ChannelType == TEXT("Xrotation"))
          Channel->Type = EInterchangeBVHChannelEnum::X_ROTATION;
        else if (ChannelType == TEXT("Yrotation"))
          Channel->Type = EInterchangeBVHChannelEnum::Y_ROTATION;
        else if (ChannelType == TEXT("Zrotation"))
          Channel->Type = EInterchangeBVHChannelEnum::Z_ROTATION;
        else {
          // Unknown channel type, clean up and return false
          UE_LOG(LogTemp, Error, TEXT("Unknown channel type: %s"),
                 *ChannelType);
          delete Channel;
          Clear();
          return false;
        }

        Channels.Add(Channel);
        if (CurrentJoint) {
          CurrentJoint->Channels.Add(Channel);
        }
      }
    } else if (Token == TEXT("MOTION")) {
      break;
    }
  }

  // Parse Motion Section
  while (TokenIndex < Tokens.Num()) {
    Token = GetNextToken();
    if (Token == TEXT("Frames:")) {
      NumFrames = FCString::Atoi(*GetNextToken());
    } else if (Token == TEXT("Frame") && PeekNextToken() == TEXT("Time:")) {
      GetNextToken(); // Time:
      FrameTime = FCString::Atod(*GetNextToken());
      break;
    }
  }

  int32 TotalValues = NumFrames * Channels.Num();
  MotionData.Reserve(TotalValues);

  for (int32 i = 0; i < TotalValues; ++i) {
    if (TokenIndex >= Tokens.Num())
      break;
    MotionData.Add(FCString::Atod(*GetNextToken()));
  }

  UE_LOG(LogTemp, Log,
         TEXT("BVH Parsing successful. Joints: %d, Channels: %d, Frames: %d"),
         Joints.Num(), Channels.Num(), NumFrames);
  return true;
}

FTransform FInterchangeBVHParser::GetTransform(int32 FrameIndex,
                                               const FString &NodeUid) {
  if (FrameIndex == 0) {
    UE_LOG(LogTemp, Log, TEXT("GetTransform: Frame 0, NodeUid: %s"), *NodeUid);
  }
  const FInterchangeBVHJoint *Joint = GetJointFromNodeUid(NodeUid);
  if (!Joint || FrameIndex < 0 || FrameIndex >= NumFrames) {
    return FTransform::Identity;
  }

  const double *FrameData = &MotionData[FrameIndex * Channels.Num()];

  FVector LocalOffset(Joint->Offset[0], -Joint->Offset[1], Joint->Offset[2]);
  FVector Euler(0.0f, 0.0f, 0.0f);

  for (FInterchangeBVHChannel *Channel : Joint->Channels) {
    double Value = FrameData[Channel->Index];
    switch (Channel->Type) {
    case EInterchangeBVHChannelEnum::X_POSITION:
      LocalOffset.X = Value;
      break;
    case EInterchangeBVHChannelEnum::Y_POSITION:
      LocalOffset.Y = -Value;
      break;
    case EInterchangeBVHChannelEnum::Z_POSITION:
      LocalOffset.Z = Value;
      break;
    case EInterchangeBVHChannelEnum::Z_ROTATION:
      Euler.Z = -Value;
      break;
    case EInterchangeBVHChannelEnum::Y_ROTATION:
      Euler.Y = Value;
      break;
    case EInterchangeBVHChannelEnum::X_ROTATION:
      Euler.X = -Value;
      break;
    }
  }

  double RadX = FMath::DegreesToRadians(Euler.X);
  double RadY = FMath::DegreesToRadians(Euler.Y);
  double RadZ = FMath::DegreesToRadians(Euler.Z);

  FQuat RotationZ(FVector::UnitZ(), RadZ);
  FQuat RotationY(FVector::UnitY(), RadY);
  FQuat RotationX(FVector::UnitX(), RadX);
  FQuat Rotation = RotationZ * RotationY * RotationX;

  return FTransform(Rotation, LocalOffset);
}

const FInterchangeBVHJoint *
FInterchangeBVHParser::GetJointFromNodeUid(const FString &NodeUid) const {
  if (const FInterchangeBVHJoint *const *FoundJoint =
          NodeUidToJointMap.Find(NodeUid)) {
    return *FoundJoint;
  }
  return nullptr;
}

bool FInterchangeBVHParser::LoadBVHFile(
    const FString &Filename, UInterchangeBaseNodeContainer &BaseNodeContainer) {

  if (!Parse(Filename)) {
    UE_LOG(LogTemp, Error, TEXT("LoadBVHFile: Parse failed"));
    return false;
  }

  if (Joints.Num() == 0) {
    UE_LOG(LogTemp, Error, TEXT("LoadBVHFile: No joints found"));
    return false;
  }

  FInterchangeBVHJoint *RootJoint = Joints[0];

  // Create Skeleton Factory Node
  FString SkeletonUid = TEXT("Skeleton_") + FPaths::GetBaseFilename(Filename);
  UInterchangeSkeletonFactoryNode *SkeletonFactoryNode =
      NewObject<UInterchangeSkeletonFactoryNode>(&BaseNodeContainer);
  FString SkeletonDisplayName =
      FPaths::GetBaseFilename(Filename) + TEXT("_Skeleton");
  SkeletonFactoryNode->InitializeSkeletonNode(
      SkeletonUid, SkeletonDisplayName, USkeleton::StaticClass()->GetName(),
      &BaseNodeContainer);

  // Process Hierarchy to generate UIDs and Map
  TMap<FString, int32> NodeNameCount;

  // Populate NodeUidToJointMap
  for (FInterchangeBVHJoint *Joint : Joints) {
    FString NodeName = Joint->Name;
    int32 &Count = NodeNameCount.FindOrAdd(NodeName);
    FString NodeUid =
        TEXT("SceneNode_") + NodeName + TEXT("_") + FString::FromInt(Count);
    Count++;
    NodeUidToJointMap.Add(NodeUid, Joint); // Store the mapping
  }

  FString ActualRootNodeUid = TEXT("");
  for (const auto &Pair : NodeUidToJointMap) {
    if (Pair.Value == RootJoint) {
      ActualRootNodeUid = Pair.Key;
      break;
    }
  }

  SkeletonFactoryNode->SetCustomRootJointUid(ActualRootNodeUid);
  SkeletonFactoryNode->SetCustomUseTimeZeroForBindPose(true);
  BaseNodeContainer.AddNode(SkeletonFactoryNode);

  // Create SkeletalMesh Factory Node
  FString SkeletalMeshUid =
      TEXT("SkeletalMesh_") + FPaths::GetBaseFilename(Filename);
  UInterchangeSkeletalMeshFactoryNode *SkeletalMeshFactoryNode =
      NewObject<UInterchangeSkeletalMeshFactoryNode>(&BaseNodeContainer);
  FString SkeletalMeshDisplayName =
      FPaths::GetBaseFilename(Filename) + TEXT("_Mesh");
  SkeletalMeshFactoryNode->InitializeSkeletalMeshNode(
      SkeletalMeshUid, SkeletalMeshDisplayName,
      USkeletalMesh::StaticClass()->GetName(), &BaseNodeContainer);
  BaseNodeContainer.AddNode(SkeletalMeshFactoryNode);

  // Create Dummy Mesh Node
  FString MeshUid = TEXT("Mesh_") + FPaths::GetBaseFilename(Filename);
  UInterchangeMeshNode *MeshNode =
      NewObject<UInterchangeMeshNode>(&BaseNodeContainer);
  MeshNode->InitializeNode(MeshUid, SkeletalMeshDisplayName,
                           EInterchangeNodeContainerType::TranslatedAsset);
  MeshNode->SetPayLoadKey(MeshUid, EInterchangeMeshPayLoadType::SKELETAL);
  MeshNode->SetSkinnedMesh(true);
  BaseNodeContainer.AddNode(MeshNode);

  // Create SkeletalMesh LOD Data Node
  FString LodDataUid = TEXT("LodData_") + FPaths::GetBaseFilename(Filename);
  UInterchangeSkeletalMeshLodDataNode *LodDataNode =
      NewObject<UInterchangeSkeletalMeshLodDataNode>(&BaseNodeContainer);
  LodDataNode->InitializeNode(LodDataUid, TEXT("LOD0"),
                              EInterchangeNodeContainerType::FactoryData);
  LodDataNode->SetCustomSkeletonUid(SkeletonUid);
  LodDataNode->AddMeshUid(MeshUid);
  BaseNodeContainer.AddNode(LodDataNode);

  // Link SkeletalMesh to LOD Data
  SkeletalMeshFactoryNode->AddLodDataUniqueId(LodDataUid);

  // Explicitly add dependency on Skeleton Factory Node to ensure it runs first
  SkeletalMeshFactoryNode->AddTargetNodeUid(SkeletonUid);

  // Create AnimSequence Factory Node
  FString AnimSequenceUid =
      TEXT("AnimSequence_") + FPaths::GetBaseFilename(Filename);
  UInterchangeAnimSequenceFactoryNode *AnimSequenceFactoryNode =
      NewObject<UInterchangeAnimSequenceFactoryNode>(&BaseNodeContainer);
  AnimSequenceFactoryNode->InitializeAnimSequenceNode(
      AnimSequenceUid,
      *FString::Printf(TEXT("%s_Anim"), *FPaths::GetBaseFilename(Filename)),
      &BaseNodeContainer);

  UE_LOG(LogTemp, Log, TEXT("SkeletonUid: %s"), *SkeletonUid);
  UE_LOG(LogTemp, Log, TEXT("SkeletalMeshUid: %s"), *SkeletalMeshUid);
  UE_LOG(LogTemp, Log, TEXT("AnimSequenceUid: %s"), *AnimSequenceUid);
  UE_LOG(LogTemp, Log, TEXT("ActualRootNodeUid: %s"), *ActualRootNodeUid);

  // Link to Skeleton Factory Node
  if (!AnimSequenceFactoryNode->SetCustomSkeletonFactoryNodeUid(SkeletonUid)) {
    UE_LOG(LogTemp, Error,
           TEXT("Failed to set CustomSkeletonFactoryNodeUid on "
                "AnimSequenceFactoryNode"));
  }

  AnimSequenceFactoryNode->SetCustomImportBoneTracks(true);
  if (FrameTime > 0.0) {
    AnimSequenceFactoryNode->SetCustomImportBoneTracksSampleRate(1.0 /
                                                                 FrameTime);
  }

  // Ensure AnimSequence runs after SkeletalMesh (which populates the Skeleton)
  AnimSequenceFactoryNode->AddTargetNodeUid(SkeletalMeshUid);
  AnimSequenceFactoryNode->AddTargetNodeUid(SkeletonUid);

  BaseNodeContainer.AddNode(AnimSequenceFactoryNode);

  TMap<FString, FString> SceneNodeAnimationPayloadKeyUids;
  TMap<FString, uint8> SceneNodeAnimationPayloadKeyTypes;

  for (FInterchangeBVHJoint *Joint : Joints) {
    FString NodeUid = TEXT("");
    for (const auto &Pair : NodeUidToJointMap) // Find the UID for this joint
    {
      if (Pair.Value == Joint) {
        NodeUid = Pair.Key;
        break;
      }
    }
    if (NodeUid.IsEmpty())
      continue; // Should not happen if map is correctly populated

    UInterchangeSceneNode *SceneNode =
        NewObject<UInterchangeSceneNode>(&BaseNodeContainer);
    SceneNode->InitializeNode(NodeUid, Joint->Name,
                              EInterchangeNodeContainerType::TranslatedScene);

    SceneNode->AddSpecializedType(
        UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());

    // Set Transform (Local Offset)
    // BVH Offset is X, Y, Z. Convert to UE: X, -Z, Y (matching GetTransform
    // logic)
    FTransform LocalTransform;
    LocalTransform.SetLocation(
        FVector(Joint->Offset[0], -Joint->Offset[1], Joint->Offset[2]));
    LocalTransform.SetRotation(FQuat::Identity);
    LocalTransform.SetScale3D(FVector::OneVector);
    SceneNode->SetCustomLocalTransform(&BaseNodeContainer, LocalTransform);

    // Set Hierarchy
    if (Joint->Parent) {
      FString ParentUid = TEXT("");
      for (const auto &Pair :
           NodeUidToJointMap) // Find the UID for parent joint
      {
        if (Pair.Value == Joint->Parent) {
          ParentUid = Pair.Key;
          break;
        }
      }
      if (!ParentUid.IsEmpty()) {
        BaseNodeContainer.SetNodeParentUid(NodeUid, ParentUid);
      }
    }

    BaseNodeContainer.AddNode(SceneNode);

    // Add to Animation Payload Map
    FString PayloadKey = Filename + TEXT("|") + NodeUid;
    SceneNodeAnimationPayloadKeyUids.Add(NodeUid, PayloadKey);
    SceneNodeAnimationPayloadKeyTypes.Add(
        NodeUid, (uint8)EInterchangeAnimationPayLoadType::BAKED);
  }

  AnimSequenceFactoryNode->SetAnimationPayloadKeysForSceneNodeUids(
      SceneNodeAnimationPayloadKeyUids, SceneNodeAnimationPayloadKeyTypes);

  UE_LOG(LogTemp, Log, TEXT("LoadBVHFile: Completed successfully"));
  return true;
}

} // namespace Interchange
} // namespace UE
