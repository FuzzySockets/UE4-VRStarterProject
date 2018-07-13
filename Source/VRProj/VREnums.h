#pragma once
#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class ESide : uint8 {
  Left  UMETA(DisplayName = "Left"),
  Right UMETA(DisplayName = "Right")
};