#pragma once
class Activity { public: int tag = 1; };
class ActivityManager {
 public:
  bool resumeNativeAppLoop(Activity*);
};
extern ActivityManager activityManager;
