#pragma once
#include "BVHFactory.generated.h"
#include "CoreMinimal.h"
#include "Factories/Factory.h"

UCLASS()
class UBVHFactory : public UFactory {
  GENERATED_BODY()

public:
  UBVHFactory();

  virtual UObject *FactoryCreateFile(UClass *InClass, UObject *InParent,
                                     FName InName, EObjectFlags Flags,
                                     const FString &Filename,
                                     const TCHAR *Parms, FFeedbackContext *Warn,
                                     bool &bOutOperationCanceled) override;
  virtual bool FactoryCanImport(const FString &Filename) override;
};
