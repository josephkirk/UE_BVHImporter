#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

namespace UE {
namespace Interchange {
class FInterchangeBVHParser {
public:
  FInterchangeBVHParser();
  ~FInterchangeBVHParser();

  void LoadBVHFile(const FString &Filename,
                   UInterchangeBaseNodeContainer &BaseNodeContainer);

private:
};
} // namespace Interchange
} // namespace UE
