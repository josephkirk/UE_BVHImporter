#include "BVHImporterModule.h"
#include "Engine/Engine.h"
#include "InterchangeBVHTranslator.h"
#include "InterchangeManager.h"
#include "Misc/CoreDelegates.h"


#define LOCTEXT_NAMESPACE "FBVHImporterModule"

void FBVHImporterModule::StartupModule() {
  auto RegisterItems = []() {
    UInterchangeManager &InterchangeManager =
        UInterchangeManager::GetInterchangeManager();
    InterchangeManager.RegisterTranslator(
        UInterchangeBVHTranslator::StaticClass());
  };

  if (GEngine) {
    RegisterItems();
  } else {
    FCoreDelegates::OnPostEngineInit.AddLambda(RegisterItems);
  }
}

void FBVHImporterModule::ShutdownModule() {
  // No explicit unregister needed as InterchangeManager cleans up or it
  // persists until shutdown
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBVHImporterModule, BVHImporter)
