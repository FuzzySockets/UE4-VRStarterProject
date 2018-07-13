#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VRHandController.generated.h"

class UMotionControllerComponent;
class UHapticFeedbackEffect_Base;

UCLASS()
class VRPROJ_API AVRHandController : public AActor {

  GENERATED_BODY()

  public:
    AVRHandController();
    static AVRHandController* Create(UObject* WorldContextObject, USceneComponent* AttachTo, const FName TrackingSource, UClass* Class);
    virtual void Tick(float DeltaTime) override;
    void Grip();
    void Release();

  protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UMotionControllerComponent* MotionController;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    UHapticFeedbackEffect_Base* CanClimbEffect;

  private:
    bool bCanClimb = false;
    bool bIsClimbing = false;
    bool CanClimb();
    FVector ClimbingStartLocation;

    UFUNCTION()
    void HandleOverlap(AActor* OverlappedActor, AActor* OtherActor);

    UFUNCTION()
    void HandleEndOverlap(AActor* OverlappedActor, AActor* OtherActor);
};
