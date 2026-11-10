// Copyright JTW Studio 2026. All Rights Reserved.

#pragma once

// 联网自动化测试专用类。UHT 不理会 #if，无法用宏把反射类从 Shipping 里排除（试过，会导致 UHT 生成的 .gen.cpp 引用未声明的类而编译失败），
// 所以它们在 Shipping 里作为 Transient 空壳存在；只有构造函数里的几行设置，没有任何测试逻辑。
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "RMSNetTestTypes.generated.h"

/** 联网自动化测试专用角色：悬空飞行、无重力，让位移断言不受地面和重力干扰。 */
UCLASS(Transient, NotBlueprintable)
class ARMSNetTestCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARMSNetTestCharacter();
};

/** 联网自动化测试专用 GameMode：玩家出生为 ARMSNetTestCharacter。 */
UCLASS(Transient, NotBlueprintable)
class ARMSNetTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARMSNetTestGameMode();
};
