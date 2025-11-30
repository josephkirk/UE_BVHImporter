#include "InterchangeBVHParser.h"
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
#include "Misc/Paths.h"

namespace UE {
namespace Interchange {
FInterchangeBVHParser::FInterchangeBVHParser() {}

FInterchangeBVHParser::~FInterchangeBVHParser() {}

void FInterchangeBVHParser::LoadBVHFile(
    const FString &Filename, UInterchangeBaseNodeContainer &BaseNodeContainer) {
  if (!FPaths::FileExists(Filename)) {
    return;
  }

  FBVHParser Parser(Filename);
  FBVHData Data;
  if (!Parser.Parse(Data) || !Data.RootNode.IsValid()) {
    return;
  }

  // Create Skeleton Factory Node
  FString SkeletonUid = TEXT("Skeleton_") + FPaths::GetBaseFilename(Filename);
  UInterchangeSkeletonFactoryNode *SkeletonFactoryNode =
      NewObject<UInterchangeSkeletonFactoryNode>(&BaseNodeContainer);
  FString SkeletonDisplayName =
      FPaths::GetBaseFilename(Filename) + TEXT("_Skeleton");
  SkeletonFactoryNode->InitializeSkeletonNode(
      SkeletonUid, SkeletonDisplayName, USkeleton::StaticClass()->GetPathName(),
      &BaseNodeContainer);

  FString RootNodeUid =
      FString::Printf(TEXT("%s_%p"), *Data.RootNode->Name, Data.RootNode.Get());
  SkeletonFactoryNode->SetCustomRootJointUid(RootNodeUid);
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
      USkeletalMesh::StaticClass()->GetPathName(), &BaseNodeContainer);
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
  AnimSequenceFactoryNode->SetCustomSkeletonFactoryNodeUid(SkeletonUid);
  // Ensure AnimSequence runs after SkeletalMesh (which populates the Skeleton)
  AnimSequenceFactoryNode->AddTargetNodeUid(SkeletalMeshUid);
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
}
} // namespace Interchange
} // namespace UE
