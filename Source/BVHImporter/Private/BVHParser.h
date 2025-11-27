#pragma once

#include "CoreMinimal.h"

enum class EBVHChannel : uint8
{
	Xposition,
	Yposition,
	Zposition,
	Zrotation,
	Xrotation,
	Yrotation,
	Unknown
};

struct FBVHNode
{
	FString Name;
	FVector3d Offset;
	TArray<EBVHChannel> Channels;
	TArray<TSharedPtr<FBVHNode>> Children;
	TWeakPtr<FBVHNode> Parent;
	int32 ChannelStartIndex = -1; // Start index in motion data frame

	FBVHNode() : Offset(FVector3d::ZeroVector) {}
};

struct FBVHData
{
	TSharedPtr<FBVHNode> RootNode;
	int32 NumFrames = 0;
	double FrameTime = 0.0;
	TArray<TArray<double>> MotionData; // [FrameIndex][ChannelIndex]
};

class BVHIMPORTER_API FBVHParser
{
public:
	FBVHParser(const FString& InFilename);
	bool Parse(FBVHData& OutData);

private:
	FString Filename;
	TArray<FString> Lines;
	int32 CurrentLineIndex;

	bool ParseHierarchy(TSharedPtr<FBVHNode>& OutRootNode);
	bool ParseNode(TSharedPtr<FBVHNode> ParentNode, TSharedPtr<FBVHNode>& OutNode);
	bool ParseMotion(FBVHData& OutData);
	
	FString GetNextToken(FString& Line);
	bool ReadLine(FString& OutLine);
};
