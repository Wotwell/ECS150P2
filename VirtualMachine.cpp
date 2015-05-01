#include "VirtualMachine.h"
#include "Machine.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <vector>

extern "C" {
  class Thread;
  
  int _tickms;
  int _machinetickms;
  volatile unsigned long _total_ticks;
  volatile TMachineSignalStateRef _signalstate;
  std::vector<Thread> _threads;
  int _ntid;


  class Thread{
      private:
        TVMMemorySize tStackSize;
        TVMStatus tStatus;
        TVMTick tTick;
        TVMThreadID tThreadID;
        TVMMutexID tMutexID;
        TVMThreadPriority tThreadPriority;  
        TVMThreadState tThreadState;  
        TVMThreadEntry tThreadEntry;
        void* tEntryParam;
        void* tStack;
        SMachineContextRef mcntxref; 
    public:
        Thread(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
                tThreadEntry = entry;
                tEntryParam = param;
                tStackSize = memsize;
                tThreadPriority = prio;
                tThreadID = *tid;
                tStackSize = 1048576; //1MB
                tStack = malloc(tStackSize);
                tThreadState = VM_THREAD_STATE_DEAD;
                tTick = 0;
                
                MachineContextCreate( mcntxref, tThreadEntry, tEntryParam, tStack, tStackSize);
            
        }
        
  };



  TVMStatus VMStart(int tickms, int machinetickms, int argc, char *argv[]) {
    TVMMainEntry VMLoadModule(const char *module);
    void VMAlarmCallback(void* data);
    
    //printf("Hello\n");
    _ntid = 0;
    _tickms = tickms;
    _machinetickms = machinetickms;
    TVMMainEntry module_main = NULL;
    int alarmtick = 100*_tickms;
    _total_ticks = 0;
    
    
    //initialize machine
    MachineInitialize(_machinetickms);
    MachineEnableSignals();
    MachineRequestAlarm(alarmtick, VMAlarmCallback, NULL);

    //load and call module
    module_main = VMLoadModule(argv[0]);
    if(module_main == NULL)
      return VM_STATUS_FAILURE;
    module_main(argc,argv);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMFileWrite(int filedescriptor, void *data, int *length) {
    if(data == NULL || length == NULL)
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    int what_is_left;
    what_is_left = write(filedescriptor,(char *)data,*length);
    if(what_is_left == -1)
      return VM_STATUS_FAILURE;
    *length = what_is_left; // This is how we tell who called us how much we
                                     // actually wrote.
    return VM_STATUS_SUCCESS;
  }
  
  TVMStatus VMThreadSleep(TVMTick tick) {
    /*
    printf("Starting sleep, ticks at %ld\n",_total_ticks);
    unsigned long end_time = _total_ticks + tick;
    unsigned long last_tick = _total_ticks;
    while(_total_ticks < end_time) {
      //sleep(100);
      // if(last_tick != _total_ticks) {
      // 	printf("It ticked, you prick\n");
      // 	last_tick = _total_ticks;
      // }

    }
    printf("Ending sleep, ticks at %ld\n",_total_ticks);
    */
    return VM_STATUS_SUCCESS;
    
  }

  void VMAlarmCallback(void* data){
    //printf("Hello, ticks are at %ld\n", _total_ticks);
    _total_ticks++;
  }
  
  TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
    MachineSuspendSignals(_signalstate);
    ++_ntid;
    _threads.push_back(Thread(entry,param,memsize,prio,tid));
    MachineResumeSignals(_signalstate);
    return VM_STATUS_SUCCESS; //Just a dummy for compilation
  }
  
  
  

}
