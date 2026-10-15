#ifndef VR_ASYNC_H
#define VR_ASYNC_H
/* Worker is never joined while a runtime call is blocked. The host pins the DSO.
 * A single worker can be started once; callbacks never call engine console APIs.
 */
void vr_async_start(void (*task)());
bool vr_async_done();
void vr_async_finish();
#endif
