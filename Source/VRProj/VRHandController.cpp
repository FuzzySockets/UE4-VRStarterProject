#include "VRHandController.h"
#include "MotionControllerComponent.h"
#include "Engine/World.h"
#include "XRMotionControllerBase.h"
#include "GameFramework/Pawn.h"

AVRHandController::AVRHandController() {
  PrimaryActorTick.bCanEverTick = true;
  MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("Motion Controller"));
  MotionController->bDisplayDeviceModel = true;
  MotionController->bGenerateOverlapEvents = true;
  SetRootComponent(MotionController);
}

AVRHandController* AVRHandController::Create(UObject* WorldContextObject, USceneComponent* AttachTo, const FName TrackingSource, UClass* Class) {
  UWorld* World = GEngine->GetWorldFromContextObjectChecked(WorldContextObject);
  if (!Class) Class = AVRHandController::StaticClass();
  auto* HandController = World->SpawnActor<AVRHandController>(Class);
  if (!ensure(HandController)) return nullptr;
  HandController->MotionController->SetTrackingMotionSource(TrackingSource);
  HandController->AttachToComponent(AttachTo, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
  return HandController;
}

void AVRHandController::Tick(float DeltaTime) {
  Super::Tick(DeltaTime);
  if (bIsClimbing) {
    FVector HandControllerDelta = GetActorLocation() - ClimbingStartLocation;
    GetAttachParentActor()->AddActorWorldOffset(-HandControllerDelta);
  }
}

void AVRHandController::BeginPlay() {
  Super::BeginPlay();
  OnActorBeginOverlap.AddDynamic(this, &AVRHandController::HandleOverlap);
  OnActorEndOverlap.AddDynamic(this, &AVRHandController::HandleEndOverlap);
}

void AVRHandController::HandleOverlap(AActor* OverlappedActor, AActor* OtherActor) {
  if (!bCanClimb && CanClimb()) {
    if (!CanClimbEffect) return;
    EControllerHand Hand = MotionController->MotionSource == FXRMotionControllerBase::LeftHandSourceId ? EControllerHand::Left : EControllerHand::Right;
    APawn* Pawn = Cast<APawn>(GetAttachParentActor());
    if (!Pawn) return;
    auto* PlayerController = Cast<APlayerController>(Pawn->GetController());
    if (!PlayerController) return;
    GetGameInstance()->GetFirstLocalPlayerController()->PlayHapticEffect(CanClimbEffect, Hand, 1.0);
  }
}

void AVRHandController::HandleEndOverlap(AActor* OverlappedActor, AActor* OtherActor) {
  CanClimb();
}

bool AVRHandController::CanClimb() {
  TArray<AActor*> OverlappingActors;
  GetOverlappingActors(OverlappingActors);
  bCanClimb = OverlappingActors.ContainsByPredicate([](const AActor* A) {
    return A->ActorHasTag(TEXT("Climbable"));
  });
  return bCanClimb;
}

void AVRHandController::Grip() {
  if (!bCanClimb) return;
  if (!bIsClimbing) {
    ClimbingStartLocation = GetActorLocation();
  }
  bIsClimbing = true;
}

void AVRHandController::Release() {
  bIsClimbing = false;
}
