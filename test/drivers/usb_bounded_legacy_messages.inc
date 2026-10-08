/* Used only by the unchanged legacy event loop/message producer tests below.
 * Defined AFTER the bounded implementation: any queue/semaphore call added to
 * that implementation fails compilation through its missing declaration. */
typedef int BaseType_t;
typedef unsigned TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define HOST_CHECK(condition,result) do { if(!(condition)) return (result); } while(0)
#define ESP_LOGE(...) ((void)0)
static int xSemaphoreTake(SemaphoreHandle_t lock,unsigned ticks) {
    assert(ticks==portMAX_DELAY && lock); ++lock->depth; return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t lock) { sim_exit(lock); }
static int xQueueSend(QueueHandle_t queue,const void *item,unsigned ticks) {
    assert(ticks==0);
    if(queue->count==16) return pdFALSE;
    memcpy(&queue->item,item,sizeof(queue->item)); ++queue->count; return pdTRUE;
}
static int xQueueReceive(QueueHandle_t queue,void *item,unsigned ticks) {
    assert(ticks==0);
    if(!queue->count) return pdFALSE;
    memcpy(item,&queue->item,sizeof(queue->item)); --queue->count; return pdTRUE;
}
static unsigned uxQueueMessagesWaiting(QueueHandle_t queue) { return queue->count; }
static void _handle_pending_ep(client_t *client) { (void)client; abort(); }
