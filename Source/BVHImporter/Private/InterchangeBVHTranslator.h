// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/InterchangeAnimationPayloadInterface.h"
#include "BVHParser.h"
#include "CoreMinimal.h"
#include "InterchangeTranslatorBase.h"

#include "InterchangeBVHTranslator.generated.h"

UCLASS(BlueprintType, Experimental, MinimalAPI)
class UInterchangeBVHTranslator : public UInterchangeTranslatorBase,
                                  public IInterchangeAnimationPayloadInterface {
  GENERATED_BODY()

public:
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

private:
  mutable TOptional<FBVHData> CachedBVHData;
  mutable FString CachedFilename;

  const FBVHData *GetBVHData(const FString &Filename) const;
};
