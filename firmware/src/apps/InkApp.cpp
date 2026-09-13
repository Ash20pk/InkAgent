#include "InkApp.h"

namespace inkapp {

const AppInfo* registryHead = nullptr;

int appCount() {
  int count = 0;
  for (const AppInfo* app = registryHead; app != nullptr; app = app->next) {
    count++;
  }
  return count;
}

}  // namespace inkapp
