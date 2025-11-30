#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "Math/Transform.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

namespace UE {
namespace Interchange {
enum class EInterchangeBVHChannelEnum : uint8 {
  X_ROTATION,
  Y_ROTATION,
  Z_ROTATION,
  X_POSITION,
  Y_POSITION,
  Z_POSITION
};

struct FInterchangeBVHJoint;

struct FInterchangeBVHChannel {
  FInterchangeBVHJoint *Joint = nullptr;
  EInterchangeBVHChannelEnum Type = EInterchangeBVHChannelEnum::X_ROTATION;
  int32 Index = 0;
};

struct FInterchangeBVHJoint {
  FString Name;
  int32 Index = 0;
  FInterchangeBVHJoint *Parent = nullptr;
  TArray<FInterchangeBVHJoint *> Children;
  double Offset[3] = {0.0, 0.0, 0.0};
  bool bHasSite = false;
  double Site[3] = {0.0, 0.0, 0.0};
  TArray<FInterchangeBVHChannel *> Channels;
};

class FInterchangeBVHParser {
public:
  FInterchangeBVHParser();
  ~FInterchangeBVHParser();

  bool LoadBVHFile(const FString &Filename,
                   UInterchangeBaseNodeContainer &BaseNodeContainer);

  bool Parse(const FString &Filename);
  FTransform GetTransform(int32 FrameIndex, const FString &NodeUid);
  double GetFrameTime() const { return FrameTime; }
  int32 GetNumFrames() const { return NumFrames; }
  FInterchangeBVHJoint *GetRootJoint() const {
    return Joints.Num() > 0 ? Joints[0] : nullptr;
  }
  const TArray<FInterchangeBVHJoint *> &GetJoints() const { return Joints; }

private:
  void Clear();
  void OutputHierarchy(
      const FInterchangeBVHJoint *Joint, int32 IndentLevel,
      TArray<int32> &ChannelList); // Helper for debugging/saving if needed

  TArray<FInterchangeBVHJoint *> Joints;
  TArray<FInterchangeBVHChannel *> Channels;
  TMap<FString, FInterchangeBVHJoint *> JointMap;          // Name to Joint
  TMap<FString, FInterchangeBVHJoint *> NodeUidToJointMap; // UID to Joint

  TArray<double> MotionData;
  double FrameTime = 0.0;
  int32 NumFrames = 0;

  // Helper to map NodeUid back to Joint for GetTransform
  // NodeUid is constructed as "SceneNode_" + Name + "_" + Index
  const FInterchangeBVHJoint *GetJointFromNodeUid(const FString &NodeUid) const;
};
} // namespace Interchange
} // namespace UE
