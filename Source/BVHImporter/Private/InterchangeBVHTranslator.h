// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/InterchangeAnimationPayloadInterface.h"
#include "BVHParser.h" // Keep for FBVHData definition if needed by GetBVHData
#include "CoreMinimal.h"
#include "InterchangeBVHParser.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"

#include "InterchangeBVHTranslator.generated.h"

UCLASS(BlueprintType, Experimental)
class BVHIMPORTER_API UInterchangeBVHTranslator
    : public UInterchangeTranslatorBase,
      public IInterchangeAnimationPayloadInterface,
      public IInterchangeMeshPayloadInterface {
  GENERATED_BODY()

public:
  UInterchangeBVHTranslator(); // Add constructor

  /** Begin UInterchangeTranslatorBase API*/
  virtual EInterchangeTranslatorType GetTranslatorType() const override;
  virtual EInterchangeTranslatorAssetType
  GetSupportedAssetTypes() const override;
  virtual TArray<FString> GetSupportedFormats() const override;
  virtual bool
  Translate(UInterchangeBaseNodeContainer &BaseNodeContainer) const override;
  /** End UInterchangeTranslatorBase API*/

  /** IInterchangeAnimationPayloadInterface Begin */
  virtual TArray<UE::Interchange::FAnimationPayloadData>
  GetAnimationPayloadData(const TArray<UE::Interchange::FAnimationPayloadQuery>
                              &PayloadQueries) const override;
  /** IInterchangeAnimationPayloadInterface End */

  /** IInterchangeMeshPayloadInterface Begin */
  virtual TOptional<UE::Interchange::FMeshPayloadData>
  GetMeshPayloadData(const FInterchangeMeshPayLoadKey &PayLoadKey,
                     const UE::Interchange::FAttributeStorage
                         &PayloadAttributes) const override;
  /** IInterchangeMeshPayloadInterface End */

private:
  mutable TOptional<FBVHData> CachedBVHData;
  mutable FString CachedFilename;

  const FBVHData *GetBVHData(const FString &Filename) const;

  TUniquePtr<UE::Interchange::FInterchangeBVHParser> BVHParser;
};
