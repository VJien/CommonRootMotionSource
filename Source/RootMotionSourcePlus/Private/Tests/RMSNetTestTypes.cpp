// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSNetTestTypes.h"
#include "GameFramework/CharacterMovementComponent.h"

ARMSNetTestCharacter::ARMSNetTestCharacter()
{
	bReplicates = true;
	SetReplicateMovement(true);
	GetCharacterMovement()->GravityScale = 0.0f;
	GetCharacterMovement()->DefaultLandMovementMode = MOVE_Flying;
	GetCharacterMovement()->bRunPhysicsWithNoController = true;
}

ARMSNetTestGameMode::ARMSNetTestGameMode()
{
	DefaultPawnClass = ARMSNetTestCharacter::StaticClass();
}
