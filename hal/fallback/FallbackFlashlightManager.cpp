#include "Hal.h"


using namespace mozilla::hal;

namespace mozilla {
namespace hal_impl {

bool
GetFlashlightEnabled()
{
  return true;
}

void
SetFlashlightEnabled(bool aEnabled)
{
}

void
RequestCurrentFlashlightState()
{
}

void
EnableFlashlightNotifications()
{
}

void
DisableFlashlightNotifications()
{
}

} // hal_impl
} // namespace mozilla
