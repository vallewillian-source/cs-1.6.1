#include "vr_async.h"
#include <atomic>
#include <thread>
namespace {std::thread *worker;std::atomic<bool> finished{false};}
void vr_async_start(void (*task)()){
    if(worker)return;
    finished.store(false,std::memory_order_release);
    worker=new std::thread([task]{task();finished.store(true,std::memory_order_release);});
}
bool vr_async_done(){return !worker||finished.load(std::memory_order_acquire);}
void vr_async_finish(){
    if(worker&&finished.load(std::memory_order_acquire)){worker->join();delete worker;worker=nullptr;}
    // Pending OpenXR calls cannot be cancelled portably. Host keeps this DSO alive.
}
