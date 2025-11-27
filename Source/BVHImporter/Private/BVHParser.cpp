#include "BVHParser.h"
#include "Misc/FileHelper.h"

FBVHParser::FBVHParser(const FString& InFilename)
	: Filename(InFilename)
	, CurrentLineIndex(0)
{
}

bool FBVHParser::Parse(FBVHData& OutData)
{
	if (!FFileHelper::LoadFileToStringArray(Lines, *Filename))
	{
		return false;
	}

	CurrentLineIndex = 0;
	FString Line;
	
	// Expect HIERARCHY
	if (!ReadLine(Line) || Line.TrimStartAndEnd() != TEXT("HIERARCHY"))
	{
		return false;
	}

	if (!ParseHierarchy(OutData.RootNode))
	{
		return false;
	}

	// Find MOTION section
	// ParseHierarchy may consume lines up to the end of HIERARCHY block
	while (CurrentLineIndex < Lines.Num())
	{
		if (Lines[CurrentLineIndex].TrimStartAndEnd() == TEXT("MOTION"))
		{
			CurrentLineIndex++;
			break;
		}
		CurrentLineIndex++;
	}

	if (CurrentLineIndex >= Lines.Num())
	{
		return false;
	}

	return ParseMotion(OutData);
}

bool FBVHParser::ParseHierarchy(TSharedPtr<FBVHNode>& OutRootNode)
{
	FString Line;
	if (!ReadLine(Line)) return false;

	FString Token = GetNextToken(Line);
	if (Token != TEXT("ROOT"))
	{
		return false;
	}

	return ParseNode(nullptr, OutRootNode);
}

bool FBVHParser::ParseNode(TSharedPtr<FBVHNode> ParentNode, TSharedPtr<FBVHNode>& OutNode)
{
	// ParseHierarchy reads "ROOT", gets name, then calls ParseNodeContent
	
	FString CurrentLine = Lines[CurrentLineIndex - 1];
	FString Type;
	FString Name;
	
	TArray<FString> Tokens;
	CurrentLine.ParseIntoArray(Tokens, TEXT(" "), true);
	
	if (Tokens.Num() >= 2)
	{
		OutNode->Name = Tokens[1];
	}
	else
	{
		OutNode->Name = TEXT("Root");
	}

	FString Line;
	if (!ReadLine(Line) || Line.TrimStartAndEnd() != TEXT("{"))
	{
		return false;
	}

	while (ReadLine(Line))
	{
		FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed == TEXT("}"))
		{
			return true;
		}
		
		if (Trimmed.StartsWith(TEXT("OFFSET")))
		{
			TArray<FString> Parts;
			Trimmed.ParseIntoArray(Parts, TEXT(" "), true);
			if (Parts.Num() >= 4)
			{
				OutNode->Offset.X = FCString::Atod(*Parts[1]);
				OutNode->Offset.Y = FCString::Atod(*Parts[2]);
				OutNode->Offset.Z = FCString::Atod(*Parts[3]);
			}
		}
		else if (Trimmed.StartsWith(TEXT("CHANNELS")))
		{
			TArray<FString> Parts;
			Trimmed.ParseIntoArray(Parts, TEXT(" "), true);
			for (int32 i = 2; i < Parts.Num(); ++i)
			{
				FString Chan = Parts[i];
				if (Chan == TEXT("Xposition")) OutNode->Channels.Add(EBVHChannel::Xposition);
				else if (Chan == TEXT("Yposition")) OutNode->Channels.Add(EBVHChannel::Yposition);
				else if (Chan == TEXT("Zposition")) OutNode->Channels.Add(EBVHChannel::Zposition);
				else if (Chan == TEXT("Zrotation")) OutNode->Channels.Add(EBVHChannel::Zrotation);
				else if (Chan == TEXT("Xrotation")) OutNode->Channels.Add(EBVHChannel::Xrotation);
				else if (Chan == TEXT("Yrotation")) OutNode->Channels.Add(EBVHChannel::Yrotation);
				else OutNode->Channels.Add(EBVHChannel::Unknown);
			}
		}
		else if (Trimmed.StartsWith(TEXT("JOINT")))
		{
			TSharedPtr<FBVHNode> ChildNode;
			if (ParseNode(OutNode, ChildNode))
			{
				OutNode->Children.Add(ChildNode);
			}
			else
			{
				return false;
			}
		}
		else if (Trimmed.StartsWith(TEXT("End Site")))
		{
			// Treat End Site as a child node with no channels
			TSharedPtr<FBVHNode> EndNode = MakeShared<FBVHNode>();
			EndNode->Name = OutNode->Name + TEXT("_End");
			EndNode->Parent = OutNode;
			
			if (!ReadLine(Line) || Line.TrimStartAndEnd() != TEXT("{")) return false;
			
			while (ReadLine(Line))
			{
				FString EndTrimmed = Line.TrimStartAndEnd();
				if (EndTrimmed == TEXT("}")) break;
				
				if (EndTrimmed.StartsWith(TEXT("OFFSET")))
				{
					TArray<FString> Parts;
					EndTrimmed.ParseIntoArray(Parts, TEXT(" "), true);
					if (Parts.Num() >= 4)
					{
						EndNode->Offset.X = FCString::Atod(*Parts[1]);
						EndNode->Offset.Y = FCString::Atod(*Parts[2]);
						EndNode->Offset.Z = FCString::Atod(*Parts[3]);
					}
				}
			}
			OutNode->Children.Add(EndNode);
		}
	}
	
	return true;
}

bool FBVHParser::ParseMotion(FBVHData& OutData)
{
	FString Line;
	
	// Parse Frames count
	if (!ReadLine(Line)) return false;
	if (Line.TrimStartAndEnd().StartsWith(TEXT("Frames:")))
	{
		FString Val = Line.Replace(TEXT("Frames:"), TEXT("")).TrimStartAndEnd();
		OutData.NumFrames = FCString::Atoi(*Val);
	}
	
	// Parse Frame Time
	if (!ReadLine(Line)) return false;
	if (Line.TrimStartAndEnd().StartsWith(TEXT("Frame Time:")))
	{
		FString Val = Line.Replace(TEXT("Frame Time:"), TEXT("")).TrimStartAndEnd();
		OutData.FrameTime = FCString::Atod(*Val);
	}
	
	// Parse Motion Data
	while (ReadLine(Line))
	{
		TArray<FString> Parts;
		// Handle tabs and spaces
		Line = Line.Replace(TEXT("\t"), TEXT(" "));
		Line.ParseIntoArray(Parts, TEXT(" "), true);
		
		if (Parts.Num() == 0) continue;
		
		TArray<double>& FrameData = OutData.MotionData.AddDefaulted_GetRef();
		for (const FString& Part : Parts)
		{
			FrameData.Add(FCString::Atod(*Part));
		}
	}
	
	return true;
}

FString FBVHParser::GetNextToken(FString& Line)
{
	FString Trimmed = Line.TrimStartAndEnd();
	int32 SpaceIdx;
	if (Trimmed.FindChar(' ', SpaceIdx))
	{
		return Trimmed.Left(SpaceIdx);
	}
	return Trimmed;
}

bool FBVHParser::ReadLine(FString& OutLine)
{
	if (CurrentLineIndex < Lines.Num())
	{
		OutLine = Lines[CurrentLineIndex++];
		return true;
	}
	return false;
}
