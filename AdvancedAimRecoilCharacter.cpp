// Copyright Epic Games, Inc. All Rights Reserved.
#include "DrawDebugHelpers.h"
#include "Kismet/KismetMathLibrary.h"
#include "AdvancedAimRecoilCharacter.h"
#include "AdvancedAimRecoilProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/InputSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MotionControllerComponent.h"
#include "XRMotionControllerBase.h" // for FXRMotionControllerBase::RightHandSourceId

DEFINE_LOG_CATEGORY_STATIC(LogFPChar, Warning, All);

//////////////////////////////////////////////////////////////////////////
// AAdvancedAimRecoilCharacter

AAdvancedAimRecoilCharacter::AAdvancedAimRecoilCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// set our turn rates for input
	BaseTurnRate = 45.f;
	BaseLookUpRate = 45.f;

	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-39.56f, 1.75f, 64.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeRotation(FRotator(1.9f, -19.19f, 5.2f));
	Mesh1P->SetRelativeLocation(FVector(-0.5f, -4.4f, -155.7f));

	// Create a gun mesh component
	FP_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FP_Gun"));
	FP_Gun->SetOnlyOwnerSee(false);			// otherwise won't be visible in the multiplayer
	FP_Gun->bCastDynamicShadow = false;
	FP_Gun->CastShadow = false;
	// FP_Gun->SetupAttachment(Mesh1P, TEXT("GripPoint"));
	FP_Gun->SetupAttachment(RootComponent);

	FP_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleLocation"));
	FP_MuzzleLocation->SetupAttachment(FP_Gun);
	FP_MuzzleLocation->SetRelativeLocation(FVector(0.2f, 48.4f, -10.6f));

	// Default offset from the character location for projectiles to spawn
	GunOffset = FVector(100.0f, 0.0f, 10.0f);

	// Note: The ProjectileClass and the skeletal mesh/anim blueprints for Mesh1P, FP_Gun, and VR_Gun 
	// are set in the derived blueprint asset named MyCharacter to avoid direct content references in C++.

	// Create VR Controllers.
	R_MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("R_MotionController"));
	R_MotionController->MotionSource = FXRMotionControllerBase::RightHandSourceId;
	R_MotionController->SetupAttachment(RootComponent);
	L_MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("L_MotionController"));
	L_MotionController->SetupAttachment(RootComponent);

	// Create a gun and attach it to the right-hand VR controller.
	// Create a gun mesh component
	VR_Gun = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VR_Gun"));
	VR_Gun->SetOnlyOwnerSee(false);			// otherwise won't be visible in the multiplayer
	VR_Gun->bCastDynamicShadow = false;
	VR_Gun->CastShadow = false;
	VR_Gun->SetupAttachment(R_MotionController);
	VR_Gun->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

	VR_MuzzleLocation = CreateDefaultSubobject<USceneComponent>(TEXT("VR_MuzzleLocation"));
	VR_MuzzleLocation->SetupAttachment(VR_Gun);
	VR_MuzzleLocation->SetRelativeLocation(FVector(0.000004, 53.999992, 10.000000));
	VR_MuzzleLocation->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));		// Counteract the rotation of the VR gun model.


		// Uncomment the following line to turn motion controllers on by default:
	//bUsingMotionControllers = true;
}

void AAdvancedAimRecoilCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Attach gun mesh component to Skeleton, doing it here because the skeleton is not yet created in the constructor
	FP_Gun->AttachToComponent(Mesh1P, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, true), TEXT("GripPoint"));

	// Show or hide the two versions of the gun based on whether or not we're using motion controllers.
	if (bUsingMotionControllers)
	{
		VR_Gun->SetHiddenInGame(false, true);
		Mesh1P->SetHiddenInGame(true, true);
	}
	else
	{
		VR_Gun->SetHiddenInGame(true, true);
		Mesh1P->SetHiddenInGame(false, true);
	}

	FOnTimelineFloat XRecoilCurve;
	FOnTimelineFloat YRecoilCurve;

	XRecoilCurve.BindUFunction(this, FName("StartHorizontalRecoil"));
	YRecoilCurve.BindUFunction(this, FName("StartVerticalRecoil"));

	if (!HorizontalCurve || !VerticalCurve)
	{
		return;
	}

	RecoilTimeline.AddInterpFloat(HorizontalCurve, XRecoilCurve);
	RecoilTimeline.AddInterpFloat(VerticalCurve, YRecoilCurve);
}
void AAdvancedAimRecoilCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (RecoilTimeline.IsPlaying()) 
	{
		RecoilTimeline.TickTimeline(DeltaSeconds);
	}
	if (RecoilTimeline.IsReversing())
	{
		
		if (FMath::Abs(YawInput) > 0 || FMath::Abs(PitchInput) > 0)
		{
			RecoilTimeline.Stop(); 
			return;
		}

		RecoilTimeline.TickTimeline(DeltaSeconds);

		FRotator NewRotation = UKismetMathLibrary::RInterpTo(GetControlRotation(),StartRotation,DeltaSeconds,2.0f);
	
		
		Controller->ClientSetRotation(NewRotation);
	}
}

//////////////////////////////////////////////////////////////////////////
// Input

void AAdvancedAimRecoilCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// set up gameplay key bindings
	check(PlayerInputComponent);

	// Bind jump events
	PlayerInputComponent->BindAction("Jump", IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction("Jump", IE_Released, this, &ACharacter::StopJumping);

	// Bind fire event
	PlayerInputComponent->BindAction("Fire", IE_Pressed, this, &AAdvancedAimRecoilCharacter::OnStartFire);
	PlayerInputComponent->BindAction("Fire", IE_Released, this, &AAdvancedAimRecoilCharacter::OnStopFire);

	PlayerInputComponent->BindAction("Reload", IE_Pressed, this, &AAdvancedAimRecoilCharacter::OnStartReload);

	// Enable touchscreen input
	EnableTouchscreenMovement(PlayerInputComponent);

	PlayerInputComponent->BindAction("ResetVR", IE_Pressed, this, &AAdvancedAimRecoilCharacter::OnResetVR);

	// Bind movement events
	PlayerInputComponent->BindAxis("MoveForward", this, &AAdvancedAimRecoilCharacter::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AAdvancedAimRecoilCharacter::MoveRight);

	// We have 2 versions of the rotation bindings to handle different kinds of devices differently
	// "turn" handles devices that provide an absolute delta, such as a mouse.
	// "turnrate" is for devices that we choose to treat as a rate of change, such as an analog joystick
	PlayerInputComponent->BindAxis("Turn", this, &AAdvancedAimRecoilCharacter::Turn);
	PlayerInputComponent->BindAxis("TurnRate", this, &AAdvancedAimRecoilCharacter::TurnAtRate);
	PlayerInputComponent->BindAxis("LookUp", this, &AAdvancedAimRecoilCharacter::LookUp);
	PlayerInputComponent->BindAxis("LookUpRate", this, &AAdvancedAimRecoilCharacter::LookUpAtRate);
}

void AAdvancedAimRecoilCharacter::OnFire()
{
	// try and fire a projectile
	if (MagazineAmmo>0)
	{
		UWorld* const World = GetWorld();
		if (World != nullptr)
		{
			
			MagazineAmmo -- ;
			FHitResult Hit;
			FVector CameraLocation;
			FRotator CameraRotation;
			Controller->GetPlayerViewPoint(CameraLocation, CameraRotation);
			FCollisionQueryParams Params;
			Params.AddIgnoredActor(this);

			FVector TraceStart = CameraLocation;
			FVector TraceEnd = TraceStart + CameraRotation.Vector() * 10000;

			bool bHasHitSomething = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
			DrawDebugLine(World, TraceStart, TraceEnd, FColor::Red, false, 3.0f, 0, 0.5f);

			if (bHasHitSomething)
			{
				DrawDebugBox(World, Hit.Location, FVector(15), FColor::Green, false, 3.0f, 0, 3.0f);
			}
			if (FireSound != nullptr)
			{
				UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
			}

			// try and play a firing animation if specified
			if (FireAnimation != nullptr)
			{
				// Get the animation object for the arms mesh
				UAnimInstance* AnimInstance = Mesh1P->GetAnimInstance();
				if (AnimInstance != nullptr)
				{
					AnimInstance->Montage_Play(FireAnimation, 1.f);
				}
			}
			else
			{
				OnStopFire();
			}
		}
	}

	// try and play the sound if specified
	
}

void AAdvancedAimRecoilCharacter::OnResetVR()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition();
}

void AAdvancedAimRecoilCharacter::BeginTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == true)
	{
		return;
	}
	if ((FingerIndex == TouchItem.FingerIndex) && (TouchItem.bMoved == false))
	{
		OnFire();
	}
	TouchItem.bIsPressed = true;
	TouchItem.FingerIndex = FingerIndex;
	TouchItem.Location = Location;
	TouchItem.bMoved = false;
}

void AAdvancedAimRecoilCharacter::EndTouch(const ETouchIndex::Type FingerIndex, const FVector Location)
{
	if (TouchItem.bIsPressed == false)
	{
		return;
	}
	TouchItem.bIsPressed = false;
}

//Commenting this section out to be consistent with FPS BP template.
//This allows the user to turn without using the right virtual joystick

//void AAdvancedAimRecoilCharacter::TouchUpdate(const ETouchIndex::Type FingerIndex, const FVector Location)
//{
//	if ((TouchItem.bIsPressed == true) && (TouchItem.FingerIndex == FingerIndex))
//	{
//		if (TouchItem.bIsPressed)
//		{
//			if (GetWorld() != nullptr)
//			{
//				UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
//				if (ViewportClient != nullptr)
//				{
//					FVector MoveDelta = Location - TouchItem.Location;
//					FVector2D ScreenSize;
//					ViewportClient->GetViewportSize(ScreenSize);
//					FVector2D ScaledDelta = FVector2D(MoveDelta.X, MoveDelta.Y) / ScreenSize;
//					if (FMath::Abs(ScaledDelta.X) >= 4.0 / ScreenSize.X)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.X * BaseTurnRate;
//						AddControllerYawInput(Value);
//					}
//					if (FMath::Abs(ScaledDelta.Y) >= 4.0 / ScreenSize.Y)
//					{
//						TouchItem.bMoved = true;
//						float Value = ScaledDelta.Y * BaseTurnRate;
//						AddControllerPitchInput(Value);
//					}
//					TouchItem.Location = Location;
//				}
//				TouchItem.Location = Location;
//			}
//		}
//	}
//}

void AAdvancedAimRecoilCharacter::MoveForward(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorForwardVector(), Value);
	}
}

void AAdvancedAimRecoilCharacter::MoveRight(float Value)
{
	if (Value != 0.0f)
	{
		// add movement in that direction
		AddMovementInput(GetActorRightVector(), Value);
	}
}

void AAdvancedAimRecoilCharacter::Turn(float Value)
{
	YawInput = Value;
	AddControllerYawInput(Value);
}
void AAdvancedAimRecoilCharacter::LookUp(float Value)
{
	PitchInput = Value;
	AddControllerPitchInput(Value);
}
void AAdvancedAimRecoilCharacter::TurnAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerYawInput(Rate * BaseTurnRate * GetWorld()->GetDeltaSeconds());
}

void AAdvancedAimRecoilCharacter::LookUpAtRate(float Rate)
{
	// calculate delta for this frame from the rate information
	AddControllerPitchInput(Rate * BaseLookUpRate * GetWorld()->GetDeltaSeconds());
}

void AAdvancedAimRecoilCharacter::OnStartFire()
{
	if (MagazineAmmo <= 0)
	{
		return;
	}
	StartRotation = GetControlRotation();
	OnFire();
	StartRecoil();
	GetWorld()->GetTimerManager().SetTimer(AutomaticFireHandle, this, &AAdvancedAimRecoilCharacter::OnFire, 0.1, true);
}
void AAdvancedAimRecoilCharacter::OnStopFire()
{
	GetWorld()->GetTimerManager().ClearTimer(AutomaticFireHandle);
	ReverseRecoil();
}

void AAdvancedAimRecoilCharacter::OnStartReload()
{
	
	FTimerHandle ReloadHandle;
	GetWorld()->GetTimerManager().SetTimer(ReloadHandle, this, &AAdvancedAimRecoilCharacter::Reload, 1.25);
}
void AAdvancedAimRecoilCharacter::Reload()
{
	
	if (MagazineAmmo < 30 &&  ReserveAmmo > 0)
	{
		if (ReserveAmmo > 29)
		{
			ReloadAmmo = 30 - MagazineAmmo;
			MagazineAmmo = MagazineAmmo + ReloadAmmo;
			ReserveAmmo = ReserveAmmo - ReloadAmmo;
		}
		else if (ReserveAmmo > 0 && ReserveAmmo < 30)
		{
			ReloadAmmo = 30 - MagazineAmmo;
			if (ReloadAmmo >= ReserveAmmo)
			{
				MagazineAmmo = MagazineAmmo + ReserveAmmo;
				ReserveAmmo = 0;
			}
			else
			{
				ReloadAmmo = 30 - MagazineAmmo;
				MagazineAmmo = MagazineAmmo + ReloadAmmo;
				ReserveAmmo = ReserveAmmo - ReloadAmmo;
			}
		}
		
	}
	
	
}
void AAdvancedAimRecoilCharacter::StartHorizontalRecoil(float Value)
{
	if (RecoilTimeline.IsReversing())
	{
		return;
	}
	AddControllerYawInput(Value);
}
void AAdvancedAimRecoilCharacter::StartVerticalRecoil(float Value)
{
	if (RecoilTimeline.IsReversing())
	{
		 return;
	}
	AddControllerPitchInput(Value);
}
void AAdvancedAimRecoilCharacter::StartRecoil()
{
	RecoilTimeline.PlayFromStart();
}
void AAdvancedAimRecoilCharacter::ReverseRecoil()
{
	RecoilTimeline.ReverseFromEnd();
}

void AAdvancedAimRecoilCharacter::TouchUpdate(const ETouchIndex::Type, const FVector Location)
{

}

bool AAdvancedAimRecoilCharacter::EnableTouchscreenMovement(class UInputComponent* PlayerInputComponent)
{
	if (FPlatformMisc::SupportsTouchInput() || GetDefault<UInputSettings>()->bUseMouseForTouch)
	{
		PlayerInputComponent->BindTouch(EInputEvent::IE_Pressed, this, &AAdvancedAimRecoilCharacter::BeginTouch);
		PlayerInputComponent->BindTouch(EInputEvent::IE_Released, this, &AAdvancedAimRecoilCharacter::EndTouch);

		//Commenting this out to be more consistent with FPS BP template.
		//PlayerInputComponent->BindTouch(EInputEvent::IE_Repeat, this, &AAdvancedAimRecoilCharacter::TouchUpdate);
		return true;
	}
	
	return false;
}
