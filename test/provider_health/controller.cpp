/* Complete, unchanged production controller plus opt-in observer. IDF/RTOS,
 * native PHY and VBUS are modeled; all controller startup/read/cleanup state
 * transitions and the final health callback are the real implementation. */
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../../Drivers/usb_controller_esp32s3/driver.health.cpp"
rtc_test_t RTCCNTL{};dwc_test_t USB_DWC{};wrap_test_t USB_WRAP{};
static unsigned io_calls,ticks,queued;
static usb_host_client_event_cb_t event_callback;
static void *event_context;
static usb_transfer_t *submitted[32];
static bool native_owned,power_owned,closing,stuck,fail_cleanup,fail_clear,fail_power_quiesce,fail_power_release,fail_native_release,no_client_event,fail_descriptor,accept_owned,deferred_close;
static usb_device_desc_t device_descriptor{0x1234,0x5678};
static const uint8_t configuration_bytes[]={9,2,25,0,1,1,0,0x80,50,9,4,0,0,1,3,1,2,0,7,5,0x81,3,8,0,10};
TickType_t xTaskGetTickCount(){return ticks;}
void vTaskDelay(TickType_t t){++io_calls;ticks+=t;}
esp_err_t usb_new_phy(const usb_phy_config_t*,usb_phy_handle_t*out){++io_calls;assert(native_owned);*out=(void*)1;return ESP_OK;}
esp_err_t usb_del_phy(usb_phy_handle_t){++io_calls;return fail_cleanup?ESP_ERR_TIMEOUT:ESP_OK;}
esp_err_t usb_phy_action(usb_phy_handle_t,int){++io_calls;return ESP_OK;}
esp_err_t usb_host_install(const usb_host_config_t*){++io_calls;return ESP_OK;}
esp_err_t usb_host_uninstall(){++io_calls;return ESP_OK;}
esp_err_t usb_host_client_register(const usb_host_client_config_t*c,usb_host_client_handle_t*out){++io_calls;event_callback=c->async.client_event_callback;event_context=c->async.callback_arg;*out=(void*)2;return ESP_OK;}
esp_err_t usb_host_client_deregister(usb_host_client_handle_t){++io_calls;return ESP_OK;}
esp_err_t usb_host_lib_handle_events(unsigned t,uint32_t*out){++io_calls;ticks+=t;*out=no_client_event?0:USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS|USB_HOST_LIB_EVENT_FLAGS_ALL_FREE;return ESP_OK;}
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t,unsigned t){++io_calls;ticks+=t;
 if(queued){usb_host_client_event_msg_t e{};e.event=USB_HOST_CLIENT_EVENT_NEW_DEV;e.new_dev.address=1;queued=0;event_callback(&e,event_context);}
 if(closing&&!stuck)for(auto &dma:submitted)if(dma){auto*p=dma;dma=nullptr;p->status=USB_TRANSFER_STATUS_CANCELED;p->actual_num_bytes=0;p->callback(p);}
 return ESP_OK;}
esp_err_t usb_host_device_open(usb_host_client_handle_t,uint8_t,usb_device_handle_t*out){++io_calls;*out=(void*)3;return ESP_OK;}
esp_err_t usb_host_device_close(usb_host_client_handle_t,usb_device_handle_t){++io_calls;return ESP_OK;}
esp_err_t usb_host_device_free_all(){++io_calls;return ESP_OK;}
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t,const usb_device_desc_t**out){++io_calls;if(fail_descriptor)return ESP_ERR_TIMEOUT;*out=&device_descriptor;return ESP_OK;}
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t,const usb_config_desc_t**out){++io_calls;*out=(const usb_config_desc_t*)configuration_bytes;return ESP_OK;}
esp_err_t usb_host_interface_claim(usb_host_client_handle_t,usb_device_handle_t,uint8_t,uint8_t){++io_calls;return ESP_OK;}
esp_err_t usb_host_interface_release(usb_host_client_handle_t,usb_device_handle_t,uint8_t){++io_calls;return fail_cleanup?ESP_ERR_TIMEOUT:ESP_OK;}
esp_err_t usb_host_transfer_alloc(size_t n,int,usb_transfer_t**out){++io_calls;*out=(usb_transfer_t*)calloc(1,sizeof(usb_transfer_t));(*out)->data_buffer=(uint8_t*)calloc(n,1);(*out)->data_buffer_size=n;return ESP_OK;}
esp_err_t usb_host_transfer_free(usb_transfer_t*p){++io_calls;for(auto*q:submitted)assert(q!=p);free(p->data_buffer);free(p);return ESP_OK;}
esp_err_t usb_host_transfer_submit(usb_transfer_t*p){++io_calls;for(auto&q:submitted)if(!q){q=p;return ESP_OK;}assert(false);return ESP_ERR_NO_MEM;}
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t,usb_transfer_t*p){return usb_host_transfer_submit(p);}
esp_err_t usb_host_endpoint_halt(usb_device_handle_t,uint8_t){++io_calls;return ESP_OK;}
esp_err_t usb_host_endpoint_flush(usb_device_handle_t,uint8_t){++io_calls;closing=true;return ESP_OK;}
esp_err_t usb_host_endpoint_clear(usb_device_handle_t,uint8_t){++io_calls;return fail_clear?ESP_ERR_INVALID_STATE:ESP_OK;}
extern "C" {
esp_err_t risc_usb_host_library_idle(usb_host_client_handle_t){++io_calls;return ESP_OK;}
esp_err_t risc_usb_host_client_step(usb_host_client_handle_t,void*,bool*out){++io_calls;*out=false;return ESP_OK;}
esp_err_t risc_usb_host_bulk_resolve(usb_host_client_handle_t,usb_device_handle_t,uint8_t,uint8_t,uint8_t,void**out,uint16_t*packet){++io_calls;if(!accept_owned)return ESP_ERR_NOT_SUPPORTED;*out=(void*)4;*packet=8;return ESP_OK;}
esp_err_t risc_usb_host_bulk_submit(void*,usb_transfer_t*){++io_calls;return accept_owned?ESP_OK:ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_host_bulk_cancel_step(void*){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_host_bulk_clear(void*){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_admission_prepare(){++io_calls;return ESP_OK;}
esp_err_t risc_usb_admission_dispose(){++io_calls;return ESP_OK;}
size_t risc_usb_admission_internal_bytes(){return 0;}size_t risc_usb_admission_dma_bytes(){return 0;}
bool risc_usb_admission_owns_resources(){return false;}bool risc_usb_admission_faulted(){return false;}
esp_err_t risc_usb_configuration_copy(usb_host_client_handle_t,usb_device_handle_t,uint8_t*,size_t,size_t*,uint16_t*,uint16_t*){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_claim_begin(usb_host_client_handle_t,usb_device_handle_t,uint8_t,uint8_t,uint64_t){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_claim_step(uint64_t){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_claim_release_step(uint64_t){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
void risc_usb_claim_retain(uint64_t){++io_calls;}bool risc_usb_claim_abandon_unacquired(uint64_t){++io_calls;return false;}
bool risc_usb_claim_state_copy(uint64_t,risc_usb_claim_state*){++io_calls;return false;}
esp_err_t risc_usb_device_close_try(usb_host_client_handle_t,usb_device_handle_t,bool*closed,bool*deferred){++io_calls;if(!deferred_close)return ESP_ERR_NOT_SUPPORTED;*closed=*deferred=true;return ESP_OK;}
esp_err_t risc_usb_control_submit(usb_host_client_handle_t,usb_transfer_t*,uint64_t){++io_calls;return accept_owned?ESP_OK:ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_control_poll(uint64_t){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t risc_usb_control_cancel(uint64_t){++io_calls;return ESP_ERR_NOT_SUPPORTED;}
void risc_usb_control_retain(uint64_t){++io_calls;}bool risc_usb_control_busy(){return false;}bool risc_usb_control_faulted(){return false;}
}
static risc_usb_phy_resource_api_v1 native_api={1,sizeof(native_api),nullptr,1,0,
 [](void*){++io_calls;return true;},[](void*,uint64_t*out){++io_calls;assert(!native_owned);native_owned=true;*out=77;return true;},
 [](void*,uint64_t token){++io_calls;assert(token==77&&!power_owned);if(fail_native_release)return false;native_owned=false;return true;}};
static risc_usb_vbus_monitor_api_v1 power_api={{1,sizeof(power_api),nullptr,
 [](void*,uint32_t ma,uint64_t*out){++io_calls;assert(ma==500&&native_owned);power_owned=true;*out=88;return true;},
 [](void*,uint64_t t){++io_calls;assert(t==88);if(fail_power_release)return false;power_owned=false;return true;},[](void*){++io_calls;return !fail_power_quiesce;}},
 [](void*)->int32_t{++io_calls;return power_owned?RISC_USB_POWER_SOURCE:RISC_USB_POWER_ABSENT;},0};
static void check(int expected){const auto before=io_calls;for(unsigned i=0;i<5;++i)assert(risc_provider_health_v1_descriptor.check()==expected);assert(io_calls==before);}
int main(int argc,char**argv){assert(argc==2);const std::string mode=argv[1];check(RISC_PROVIDER_HEALTH_READY);
 const auto*d=t5_driver_get(2);risc_provider_dependency_v1 deps[]={{"board.power.vbus",1,&power_api},{"platform.usb.phy.resource",1,&native_api}};
 assert(d->start(deps,2));check(RISC_PROVIDER_HEALTH_READY);
 const auto*c=(const risc_usb_controller_interrupt_v1*)d->capability;risc_usb_controller_event_v1 event{};
 ticks=500;assert(c->controller.next_event(nullptr,&event)==0);ticks=1000;assert(c->controller.next_event(nullptr,&event)==0);assert(native_owned&&power_owned);check(RISC_PROVIDER_HEALTH_READY);
 if(mode=="device-exhaustion-closed"){serial=UINT64_MAX;queued=1;fail_descriptor=true;assert(c->controller.next_event(nullptr,&event)==-1);check(RISC_PROVIDER_HEALTH_READY);assert(d->quiesce());d->stop();check(RISC_PROVIDER_HEALTH_READY);puts("Post-open descriptor rejection closes exact unassigned handle: READY PASS");return 0;}
 if(mode=="device-exhaustion"){serial=UINT64_MAX;queued=1;assert(c->controller.next_event(nullptr,&event)==-1);check(RISC_PROVIDER_HEALTH_RETAINED);puts("Real device-open token exhaustion loses record: RETAINED PASS");return 0;}
 queued=1;assert(c->controller.next_event(nullptr,&event)==1);uint64_t claim=0;
 if(mode=="claim-exhaustion"){serial=UINT64_MAX;assert(!c->controller.claim(nullptr,event.physical_device,0,0,&claim));check(RISC_PROVIDER_HEALTH_RETAINED);puts("Real interface-claim token exhaustion loses record: RETAINED PASS");return 0;}assert(c->controller.claim(nullptr,event.physical_device,0,0,&claim));
 uint8_t out[64];memset(out,0xa5,sizeof(out));assert(c->interrupt_read(nullptr,claim,0x81,out,sizeof(out),1)==0);check(RISC_PROVIDER_HEALTH_READY);
 assert(interrupts[0].dma->data_buffer!=out&&interrupts[0].dma->context==&interrupts[0]);
 if(mode=="request-exhaustion"){serial=UINT64_MAX;uint64_t request=0;const auto before=io_calls;assert(begin_owned_control(event.physical_device,0x80,1,0,0,nullptr,0,100,&request)==RISC_STREAM_IO&&!request);assert(io_calls==before);check(RISC_PROVIDER_HEALTH_UNKNOWN);puts("Pre-admission request token exhaustion: UNKNOWN with no custody loss PASS");return 0;}
 if(mode=="busy"){OwnedControlPort clock;assert(ownedControl.begin(clock,transfer,(void*)3,93,0x80,1,0,0,nullptr,0,100)==RISC_STREAM_AGAIN);check(RISC_PROVIDER_HEALTH_BUSY);ownedControl.cancel(93);assert(ownedControl.step(clock,93)<0);assert(ownedControl.take(93,nullptr,0)<0);check(RISC_PROVIDER_HEALTH_READY);}
 if(mode=="control-retained"){accept_owned=true;uint8_t copied[]={1,2,3};uint64_t request=0;assert(begin_owned_control(event.physical_device,0x00,1,0,0,copied,3,100,&request)==RISC_STREAM_AGAIN);memset(copied,9,3);assert(transfer->data_buffer[8]==1);check(RISC_PROVIDER_HEALTH_BUSY);assert(step_owned_control(request)==RISC_STREAM_AGAIN);ticks+=101;assert(step_owned_control(request)==RISC_STREAM_RETAINED);check(RISC_PROVIDER_HEALTH_RETAINED);puts("Actual copied-control submit deadline: BUSY to sticky RETAINED PASS");return 0;}
 if(mode=="bulk-retained"){accept_owned=true;uint8_t copied[]={1,2,3};assert(begin_owned_bulk(claim,0x02,copied,3,100)==RISC_STREAM_AGAIN);memset(copied,9,3);assert(transfer->data_buffer[0]==1);check(RISC_PROVIDER_HEALTH_BUSY);assert(step_owned_bulk()==RISC_STREAM_AGAIN);assert(step_owned_bulk()==RISC_STREAM_AGAIN);ticks+=101;assert(step_owned_bulk()==RISC_STREAM_RETAINED);check(RISC_PROVIDER_HEALTH_RETAINED);puts("Actual copied-bulk submit deadline: BUSY to sticky RETAINED PASS");return 0;}
 if(mode=="unknown"){usb_host_client_event_msg_t e{};e.event=USB_HOST_CLIENT_EVENT_NEW_DEV;e.new_dev.address=1;for(unsigned i=0;i<17;++i)event_callback(&e,nullptr);check(RISC_PROVIDER_HEALTH_UNKNOWN);puts("Full controller event overflow: UNKNOWN without observer I/O PASS");return 0;}
 if(mode=="stall-detach"){auto*dma=submitted[0];assert(dma);submitted[0]=nullptr;dma->status=USB_TRANSFER_STATUS_STALL;dma->actual_num_bytes=0;dma->callback(dma);fail_clear=true;assert(c->interrupt_read(nullptr,claim,0x81,out,sizeof(out),1)==-1);check(RISC_PROVIDER_HEALTH_READY);}
 if(mode=="claimed-busy"){assert(!d->quiesce());check(RISC_PROVIDER_HEALTH_BUSY);}
 if(mode=="stuck")stuck=true;
 if(mode=="failed")fail_cleanup=true;
 const bool released=c->controller.release(nullptr,claim);
 if(mode=="stuck"||mode=="failed"){assert(!released&&native_owned&&power_owned);check(RISC_PROVIDER_HEALTH_RETAINED);puts("Full controller failed release/drain: exact PHY/power/DMA retention and readonly terminal health PASS");return 0;}
 assert(released);if(mode!="claimed-busy")check(RISC_PROVIDER_HEALTH_READY);
 if(mode=="admission-close-retained"){deferred_close=true;assert(begin_owned_close(event.physical_device,100)==RISC_STREAM_AGAIN);check(RISC_PROVIDER_HEALTH_BUSY);assert(step_owned_admission()==RISC_STREAM_RETAINED);check(RISC_PROVIDER_HEALTH_RETAINED);puts("Actual admission reference-close with deferred native custody: BUSY to RETAINED PASS");return 0;}
 if(mode=="vbus-tail"||mode=="power-release"||mode=="native-release"||mode=="fault-timeout"){
  if(mode=="vbus-tail")fail_power_quiesce=true;
  if(mode=="power-release")fail_power_release=true;
  if(mode=="native-release")fail_native_release=true;
  if(mode=="fault-timeout"){usb_host_client_event_msg_t e{};e.event=USB_HOST_CLIENT_EVENT_NEW_DEV;for(unsigned i=0;i<17;++i)event_callback(&e,nullptr);check(RISC_PROVIDER_HEALTH_UNKNOWN);no_client_event=true;}
  assert(!d->quiesce());check(RISC_PROVIDER_HEALTH_RETAINED);puts("Full controller unwrapped cleanup tail/fault precedence: RETAINED without further native calls PASS");return 0;
 }
 assert(d->quiesce());d->stop();assert(!native_owned&&!power_owned);check(RISC_PROVIDER_HEALTH_READY);puts("Full controller native-PHY startup, armed owned interrupt DMA, reversible owned request and checked shutdown health PASS");
}
