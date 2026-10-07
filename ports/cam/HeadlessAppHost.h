#pragma once
namespace RuntimeBoot {
// Called once from the normal provider owner after the headless graph reaches
// Running. A configured ELF executes through the ordinary app loader.
void runConfiguredDefaultApp();
}
