#pragma once
#include "CoreMinimal.h"

template<typename T>
static FString EnumToString(const FString& enumName, const T value) {
  UEnum* pEnum = FindObject<UEnum>(ANY_PACKAGE, *enumName);
  return *(pEnum ? pEnum->GetNameStringByIndex(static_cast<uint8>(value)) : "null");
}