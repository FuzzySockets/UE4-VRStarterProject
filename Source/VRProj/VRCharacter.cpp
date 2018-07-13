#include "VRCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "AI/Navigation/NavigationSystem.h"
#include "XRMotionControllerBase.h"
#include "MotionControllerComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "VREnums.h"
#include "VRHandController.h"
#include "MotionControllerComponent.h"

AVRCharacter::AVRCharacter() {
  PrimaryActorTick.bCanEverTick = true;
  bUseControllerRotationYaw = true;
  SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
  SceneComponent->SetupAttachment(GetRootComponent());
  CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera Component"));
  CameraComponent->SetupAttachment(SceneComponent);

  TeleportSplinePath = CreateDefaultSubobject<USplineComponent>(TEXT("Teleport Path"));
  TeleportSplinePath->SetupAttachment(SceneComponent);

  TeleportMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Teleport Marker"));
  TeleportMarker->SetupAttachment(GetRootComponent());
  TeleportMarker->SetVisibility(false);

  ThumbstickPressState.Add(ESide::Left, IE_Released);
  ThumbstickPressState.Add(ESide::Right, IE_Released);
}

void AVRCharacter::BeginPlay() {
  Super::BeginPlay();
  LeftHandController = AVRHandController::Create(this, SceneComponent, FXRMotionControllerBase::LeftHandSourceId, HandControllerClass);
  RightHandController = AVRHandController::Create(this, SceneComponent, FXRMotionControllerBase::RightHandSourceId, HandControllerClass);
}

void AVRCharacter::Tick(float DeltaTime) {
  Super::Tick(DeltaTime);
  if (bIsTeleporting) return;
  SyncCamera();
  TeleportOrTrace();
}

void AVRCharacter::SyncCamera() {
  FVector CameraOffset = CameraComponent->GetComponentLocation() - GetActorLocation();
  CameraOffset.Z = 0;
  AddActorWorldOffset(CameraOffset);
  SceneComponent->AddWorldOffset(-CameraOffset);
}

void AVRCharacter::MoveForward(float Throttle) {
  if (bIsTeleporting) return;
  AddMovementInput(CameraComponent->GetForwardVector(), Throttle);
}

void AVRCharacter::TurnRight(float Throttle) {
  if (bIsTeleporting) return;
  AddControllerYawInput(Throttle);
}

void AVRCharacter::StrafeRight(float Throttle) {
  if (bIsTeleporting) return;
  AddMovementInput(CameraComponent->GetRightVector(), Throttle);
}

void AVRCharacter::TeleportOrTrace() {
  const ESide* PressedSide = ThumbstickPressState.FindKey(IE_Pressed);
  // Both thumbsticks are pressed
  if (ThumbstickPressState.FindKey(IE_Released) == nullptr && TeleportTo != FVector::ZeroVector) {
    Teleport();
  // One thumbstick is pressed
  } else if (PressedSide != nullptr) {
    TeleportTrace(PressedSide);
  // Both thumbsticks are released
  } else if (PressedSide == nullptr) {
    ResetTeleportMarker();
  }
}

void AVRCharacter::Teleport() {
  if (!bIsTeleporting) {
    bIsTeleporting = true;
    TeleportFade(0.f, 1.f);
    FTimerHandle CameraFadeTimer;
    GetWorldTimerManager().SetTimer(CameraFadeTimer, this, &AVRCharacter::Teleport, TeleportFadeDuration);
    ReleaseTeleportButtons();
    return;
  }
  float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
  SetActorLocation(TeleportTo + (CapsuleHalfHeight * GetCapsuleComponent()->GetUpVector()));
  TeleportFade(1.f, 0.f);
  bIsTeleporting = false;
}

void AVRCharacter::TeleportTrace(const ESide* Side) {
  FHitResult Hit;
  FNavLocation NavLocation;
  if (!LeftHandController || !RightHandController) return;
  AVRHandController* TraceComponent = *Side == ESide::Left ? LeftHandController : RightHandController;
  FVector LookDirection = TraceComponent->GetActorForwardVector();
  FVector TraceFrom = TraceComponent->GetActorLocation();
  FPredictProjectilePathResult PathResult;
  FPredictProjectilePathParams PathParams(TeleportTraceRadius, TraceFrom, LookDirection * 1000, 2.f, ECollisionChannel::ECC_Visibility, this);
  PathParams.bTraceComplex = true;
 
  bool bHitFound = UGameplayStatics::PredictProjectilePath(this, PathParams, PathResult);
  if (!bHitFound) return ResetTeleportMarker();

  bool bNavHitFound = GetWorld()->GetNavigationSystem()->ProjectPointToNavigation(PathResult.HitResult.ImpactPoint, NavLocation, FVector(100, 100, 100));
  if (!bNavHitFound) return ResetTeleportMarker();

  UpdateTeleportSpline(PathResult.PathData, TraceComponent);
  TeleportMarker->SetWorldLocation(NavLocation.Location);
  TeleportMarker->SetVisibility(true);
  TeleportTo = NavLocation.Location;
}

void AVRCharacter::UpdateTeleportSpline(TArray<FPredictProjectilePathPointData> PathData, AVRHandController* MotionController) {
  
  HideTeleportSplines();
  TeleportSplinePath->AttachToComponent(MotionController->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
  TeleportSplinePath->ClearSplinePoints(false);

  for (int32 i = 0; i < PathData.Num(); ++i) {
    FVector PointLocation = PathData[i].Location;
    FVector LocalPosition = TeleportSplinePath->GetComponentTransform().InverseTransformPosition(PointLocation);
    FSplinePoint Point(i, LocalPosition, ESplinePointType::Curve);
    TeleportSplinePath->AddPoint(Point, false);
  }
  TeleportSplinePath->UpdateSpline();

  for (int32 i = 0; i < PathData.Num() - 1; ++i) {
    if (i >= TeleportArcMeshComponents.Num()) {
      USplineMeshComponent* ArcMeshComp = NewObject<USplineMeshComponent>(this);
      ArcMeshComp->SetMobility(EComponentMobility::Movable);
      ArcMeshComp->SetStaticMesh(TeleportArcMesh);
      ArcMeshComp->SetMaterial(0, TeleportArcMaterial);
      ArcMeshComp->AttachToComponent(TeleportSplinePath, FAttachmentTransformRules::KeepRelativeTransform);
      ArcMeshComp->RegisterComponent();
      TeleportArcMeshComponents.Add(ArcMeshComp);
    }
    USplineMeshComponent* ArcMeshComp = TeleportArcMeshComponents[i];
    ArcMeshComp->SetVisibility(true);
    FVector StartTan, EndTan, StartPos, EndPos;
    TeleportSplinePath->GetLocalLocationAndTangentAtSplinePoint(i, StartPos, StartTan);
    TeleportSplinePath->GetLocalLocationAndTangentAtSplinePoint(i + 1, EndPos, EndTan);
    ArcMeshComp->SetStartAndEnd(StartPos, StartTan, EndPos, EndTan);
  }
}

void AVRCharacter::HideTeleportSplines() {
  for (USplineMeshComponent* ArcMeshComp : TeleportArcMeshComponents) {
    ArcMeshComp->SetVisibility(false);
  }
}

void AVRCharacter::TeleportFade(float AlphaStart, float AlphaStop) {
  APlayerController* PlayerController = Cast<APlayerController>(GetController());
  if (!PlayerController) return;
  APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
  CameraManager->StartCameraFade(AlphaStart, AlphaStop, TeleportFadeDuration, FLinearColor::Black, true);
}

void AVRCharacter::ResetTeleportMarker() {
  HideTeleportSplines();
  TeleportMarker->SetVisibility(false);
  TeleportTo = FVector::ZeroVector;
}

void AVRCharacter::ReleaseTeleportButtons() {
  for (auto& Element : ThumbstickPressState) {
    ThumbstickPressState[Element.Key] = IE_Released;
  }
}

AVRHandController* AVRCharacter::GetControllerBySide(const ESide& Side) {
  return Side == ESide::Left ? LeftHandController : RightHandController;
}

template<EInputEvent InputEvent, ESide Side>
void AVRCharacter::OnThumbstickPress() {
  if (bIsTeleporting) return;
  ThumbstickPressState[Side] = InputEvent;
}

template<EInputEvent InputEvent, ESide Side>
void AVRCharacter::OnGrip() {
  return 
    InputEvent == IE_Pressed ? GetControllerBySide(Side)->Grip() :
    InputEvent == IE_Released ? GetControllerBySide(Side)->Release() : void();
};

void AVRCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
  Super::SetupPlayerInputComponent(PlayerInputComponent);
  PlayerInputComponent->BindAction(TEXT("LeftThumbstick"), IE_Pressed, this, &AVRCharacter::OnThumbstickPress<IE_Pressed, ESide::Left>);
  PlayerInputComponent->BindAction(TEXT("LeftThumbstick"), IE_Released, this, &AVRCharacter::OnThumbstickPress<IE_Released, ESide::Left>);
  PlayerInputComponent->BindAction(TEXT("RightThumbstick"), IE_Pressed, this, &AVRCharacter::OnThumbstickPress<IE_Pressed, ESide::Right>);
  PlayerInputComponent->BindAction(TEXT("RightThumbstick"), IE_Released, this, &AVRCharacter::OnThumbstickPress<IE_Released, ESide::Right>);
  PlayerInputComponent->BindAction(TEXT("GripLeft"), IE_Pressed, this, &AVRCharacter::OnGrip<IE_Pressed, ESide::Left>);
  PlayerInputComponent->BindAction(TEXT("GripLeft"), IE_Released, this, &AVRCharacter::OnGrip<IE_Released, ESide::Left>);
  PlayerInputComponent->BindAction(TEXT("GripRight"), IE_Pressed, this, &AVRCharacter::OnGrip<IE_Pressed, ESide::Right>);
  PlayerInputComponent->BindAction(TEXT("GripRight"), IE_Released, this, &AVRCharacter::OnGrip<IE_Released, ESide::Right>);
  PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AVRCharacter::MoveForward);
  PlayerInputComponent->BindAxis(TEXT("TurnRight"), this, &AVRCharacter::TurnRight);
  PlayerInputComponent->BindAxis(TEXT("StrafeRight"), this, &AVRCharacter::StrafeRight);
}