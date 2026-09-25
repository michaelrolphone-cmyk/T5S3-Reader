#include "BootActivity.h"

#include "components/StartupScreen.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  StartupScreen::boot(renderer);
}
