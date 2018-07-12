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

AVRCharacter::AVRCharacter() {
  PrimaryActorTick.bCanEverTick = true;
  bUseControllerRotationYaw = true;
  SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
  SceneComponent->SetupAttachment(GetRootComponent());
  CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera Component"));
  CameraComponent->SetupAttachment(SceneComponent);

  RightMotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("Right Motion Controller"));
  RightMotionController->SetupAttachment(SceneComponent);
  RightMotionController->bDisplayDeviceModel = true;

  LeftMotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("Left Motion Controller"));
  LeftMotionController->SetupAttachment(SceneComponent);
  LeftMotionController->bDisplayDeviceModel = true;

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
  LeftMotionController->SetTrackingMotionSource(FXRMotionControllerBase::LeftHandSourceId);
  LeftMotionController->AttachToComponent(SceneComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
  RightMotionController->SetTrackingMotionSource(FXRMotionControllerBase::RightHandSourceId);
  RightMotionController->AttachToComponent(SceneComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
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
  UMotionControllerComponent* TraceComponent = *Side == ESide::Left ? LeftMotionController : RightMotionController;
  FVector LookDirection = TraceComponent->GetForwardVector();
  FVector TraceFrom = TraceComponent->GetComponentLocation();
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

void AVRCharacter::UpdateTeleportSpline(TArray<FPredictProjectilePathPointData> PathData, UMotionControllerComponent* MotionController) {
  
  HideTeleportSplines();
  TeleportSplinePath->AttachToComponent(MotionController, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
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

template<EInputEvent InputEvent, ESide Side>
void AVRCharacter::OnThumbstickPress() {
  if (bIsTeleporting) return;
  ThumbstickPressState[Side] = InputEvent;
}

void AVRCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
  Super::SetupPlayerInputComponent(PlayerInputComponent);
  PlayerInputComponent->BindAction(TEXT("LeftThumbstick"), IE_Pressed, this, &AVRCharacter::OnThumbstickPress<IE_Pressed, ESide::Left>);
  PlayerInputComponent->BindAction(TEXT("LeftThumbstick"), IE_Released, this, &AVRCharacter::OnThumbstickPress<IE_Released, ESide::Left>);
  PlayerInputComponent->BindAction(TEXT("RightThumbstick"), IE_Pressed, this, &AVRCharacter::OnThumbstickPress<IE_Pressed, ESide::Right>);
  PlayerInputComponent->BindAction(TEXT("RightThumbstick"), IE_Released, this, &AVRCharacter::OnThumbstickPress<IE_Released, ESide::Right>);
  PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AVRCharacter::MoveForward);
  PlayerInputComponent->BindAxis(TEXT("TurnRight"), this, &AVRCharacter::TurnRight);
  PlayerInputComponent->BindAxis(TEXT("StrafeRight"), this, &AVRCharacter::StrafeRight);
}