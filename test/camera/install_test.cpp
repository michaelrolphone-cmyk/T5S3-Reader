#include "../hardware/cam/CameraInstall.h"
static const char* profile="/Drivers/cam-ov3660-profile/.package.json";
static const char* camera="/Drivers/camera-esp32s3-ov3660/.package.json";
static void reset(){files.clear();files[profile]={1,2,3};files[camera]={4,5,6};validPayload=closeGood=handoffGood=installGood=true;installs=verifications=handoffs=now=0;delayStep=1;}
int main(){
 // Reboot: byte-identical installed packages require full payload verification,
 // never attempt a same-version publication or delete a rejected staging tree.
 reset();files["/Drivers/.cam-ov3660-profile.pkg-stage"]={9};auto before=files;
 assert(RuntimeBoot::installCameraExperiment());assert(installs==0&&verifications==2&&handoffs==1);assert(files==before);
 // One installed profile plus an older camera: only the camera is upgraded.
 reset();files[camera]={4,5,7};assert(RuntimeBoot::installCameraExperiment());assert(installs==1&&verifications==1);
 // Matching manifest with corrupted payload fails closed before activation.
 reset();validPayload=false;assert(!RuntimeBoot::installCameraExperiment());assert(installs==0&&verifications==1&&handoffs==1);
 // Same-version replacement is left to the ordinary manager, never silently reused.
 reset();files[profile]={1,2,9};installGood=false;assert(!RuntimeBoot::installCameraExperiment());assert(installs==1&&verifications==0);
 // Pending replacement/removal is not hidden by the reuse optimization.
 for(auto suffix:{"pkg-backup","pkg-removing"}){reset();files[std::string("/Drivers/.cam-ov3660-profile.")+suffix]={8};assert(RuntimeBoot::installCameraExperiment());assert(installs==1&&verifications==1);}
 // I/O completion, wall-clock limits and storage handoff remain mandatory.
 reset();closeGood=false;assert(!RuntimeBoot::installCameraExperiment());assert(verifications==0&&handoffs==1);
 reset();delayStep=31000;assert(!RuntimeBoot::installCameraExperiment());assert(handoffs==1);
 reset();handoffGood=false;assert(!RuntimeBoot::installCameraExperiment());assert(handoffs==1);
 puts("camera lab install reuse/corruption/recovery/deadline tests passed");
}
