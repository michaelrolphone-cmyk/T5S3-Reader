/* Legacy default callback is included only as an ownership/restoration witness.
 * Native completion must never invoke its logger or notifier. */
#define USBH_ENTER_CRITICAL() sim_enter(&usbh_lock)
#define USBH_ENTER_CRITICAL_SAFE() USBH_ENTER_CRITICAL()
#define USBH_EXIT_CRITICAL_SAFE() USBH_EXIT_CRITICAL()
#define USBH_CHECK(c,r) do { if(!(c)) return (r); } while(0)
#define USBH_CHECK_FROM_CRIT(c,r) do { if(!(c)) { USBH_EXIT_CRITICAL(); return (r); } } while(0)
#define DEV_FLAG_ACTION_DEFAULT_PIPE_FLUSH 2u
#define DEV_FLAG_ACTION_DEFAULT_PIPE_DEQUEUE 4u
#define DEV_FLAG_ACTION_DEFAULT_PIPE_CLEAR 8u
#define USB_NOTIF_SOURCE_USBH 1
static unsigned control_logs,control_notifications;
#define ESP_LOGE(...) (++control_logs)
#define ESP_EARLY_LOGE(...) (++control_logs)
static bool control_notify(int source,bool isr,void *arg) {
    (void)isr; (void)arg; assert(source==USB_NOTIF_SOURCE_USBH);
    ++control_notifications; return false;
}
