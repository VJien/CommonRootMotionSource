// Copyright JTW Studio 2026. All Rights Reserved.

#include "RMSSettings.h"

URMSSettings::URMSSettings()
{
	CategoryName = TEXT("Plugins");
}

const URMSSettings* URMSSettings::Get()
{
	return GetDefault<URMSSettings>();
}
