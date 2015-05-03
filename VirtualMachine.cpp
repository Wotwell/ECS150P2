#include "VirtualMachine.h"
#include "Machine.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <vector>
#include <queue>
#include <climits>
extern "C" {
  class Thread;

  int _tickms;
  int _machinetickms;
  volatile unsigned long _total_ticks;
  volatile TMachineSignalStateRef _signalstate;
  std::vector<Thread*> _threads;
  TVMThreadID _ntid;
  long long unsigned int _largestprime;
  long long unsigned int _lastlargestprime;
  long long unsigned int _largesttest;
  std::queue<Thread*> highQ, medQ, lowQ, jamezQ;
//pronounced qwa
  TVMThreadID _current_thread;
  
  TVMStatus VMThreadID(TVMThreadIDRef threadref);
  Thread *getThreadByID(TVMThreadID id);
  void timetokill(void* param);
  void VMScheduleThreads();
  
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
    Thread(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadID tid){
      tThreadEntry = entry;
      tEntryParam = param;
      tStackSize = memsize;
      tThreadPriority = prio;
      tThreadID = tid;
      tStack = malloc(tStackSize);
      mcntxref = new SMachineContext;
      tThreadState = VM_THREAD_STATE_DEAD;
      tTick = 0;
      //special case for our first thread (The VM itself)
      if(tThreadEntry != NULL){
        MachineContextCreate( mcntxref, tThreadEntry, tEntryParam, tStack, tStackSize);
      }
    }
    TVMThreadID getID(){
      return tThreadID;
    }
    TVMThreadPriority getPriority(){
      return tThreadPriority;
    }
    TVMThreadState getState(){
      return tThreadState;
    }
    void setState(TVMThreadState state){
      tThreadState = state;
    }
    void run() {
      printf("In Run. Thread id: %u\n",this->tThreadID);
      setState(VM_THREAD_STATE_RUNNING);
      TVMThreadID ref;
      VMThreadID(&ref);
      printf("Grabbed reference. Thread id: %u\n",ref);
      Thread *x = getThreadByID(ref);
      // do a get priority right here, if the current running threads priority is higher than ours don't switch.
      // Actually do this in schedule silly.
      
      // Only activate it if it was activated before.
      if(x->getState() == VM_THREAD_STATE_READY || 
	 x->getState() == VM_THREAD_STATE_RUNNING) {
	x->setState(VM_THREAD_STATE_DEAD); // we set it to dead just so we can activate it immediatly.
	VMThreadActivate(ref);
      }
      printf("Switched from thread %u to thread %u\n",ref,tThreadID);
      MachineContextSwitch(x->getRef(),this->mcntxref);
    }
    SMachineContextRef getRef() {
      return mcntxref;
    }    
    ~Thread() {
      delete mcntxref;
      free(tStack);
    }

  };
  Thread *getThreadByID(TVMThreadID id) {
    for(unsigned int i = 0; i < _threads.size(); ++i) {
      if(_threads[i]->getID() == id) {
	return _threads[i];
      }
    }
    return NULL;
  }
  TVMStatus VMThreadID(TVMThreadIDRef threadref) {
    *threadref = _current_thread;
    if(threadref == NULL) {
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    return VM_STATUS_SUCCESS;
  }


  TVMStatus VMStart(int tickms, int machinetickms, int argc, char *argv[]) {
    TVMMainEntry VMLoadModule(const char *module);
    void VMAlarmCallback(void* data);
    
    _current_thread = 0;
    _ntid = 0;
    _tickms = tickms;
    _machinetickms = machinetickms;
    TVMMainEntry module_main = NULL;
    int alarmtick = 100*_tickms; // milliseconds to microseconds conversion
    _total_ticks = 0;
    _largestprime = 2;
    _lastlargestprime = 1;
    _largesttest = 3;
    
    printf("In VMStart\n");
    //initialize machine
    MachineInitialize(_machinetickms);
    MachineEnableSignals();
    
    //Create Thread for VM (ID = 0)
    //printf("Creating Main Thread: tid = %d\n",_ntid);
    VMThreadCreate(timetokill, NULL, 10, VM_THREAD_PRIORITY_NORMAL, &_ntid);
    

    //create dummy thread (ID = 1)
    VMThreadCreate(timetokill, NULL, 0x100000, 000, &_ntid); //was informed we need at least 10k
    VMThreadActivate(1);
    
    //printf("Thread %d activated\n",(_ntid-1));
    //load and call module
    module_main = VMLoadModule(argv[0]);
    if(module_main == NULL)
      return VM_STATUS_FAILURE;
    //module_main(argc,argv);
    //VMThreadCreate(module_main, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid);
    
    // We do it last to make sure it doesn't run before we activate.
    MachineRequestAlarm(alarmtick, VMAlarmCallback, NULL);
    VMScheduleThreads();
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

  TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
    printf("In VMThreadCreate, tid = %u\n",_ntid);
    MachineSuspendSignals(_signalstate);
    Thread *thread = new Thread(entry,param,memsize,prio,_ntid);
    _threads.push_back(thread);
    *tid = _threads.back()->getID();
    ++_ntid;
    MachineResumeSignals(_signalstate);
    return VM_STATUS_SUCCESS; //Just a dummy for compilation
  }
  
  void timetokill(void* param){
    printf("In timetokill.\n");
    MachineEnableSignals(); // Won't int without it.
    bool prime = true;
    while(1){
      for(long long unsigned int i = 2; i < _largesttest/2; ++i){
	if( _largesttest%i == 0 ){
	  prime = false;
	  break;
	}
      }
      if(prime){
	_largestprime = _largesttest;
	if(_largestprime > _lastlargestprime * 2) {
	  printf("Reached prime milestone: %llu\n",_largestprime);
	  _lastlargestprime = _largestprime;
	}
      }
      _largesttest=_largesttest+2;
      prime = true;
    }
  }
  
  TVMStatus VMThreadActivate(TVMThreadID thread){
    MachineSuspendSignals(_signalstate);
    printf("In VMThreadActivate, with thread %u\n",thread);
    for(unsigned int i = 0; i < _threads.size(); ++i){
        if(_threads[i]->getID() == thread){
            //thread must be dead
            if(_threads[i]->getState() != VM_THREAD_STATE_DEAD){
	      printf("WILL NOT ACTIVATE, THREAD IS NOT DEAD.\n");
                return VM_STATUS_ERROR_INVALID_STATE;
            }
            //set state
	    printf("Activated thread.\n");
            _threads[i]->setState(VM_THREAD_STATE_READY);
            //push in Q
            TVMThreadPriority prio = _threads[i]->getPriority();
            if(prio == VM_THREAD_PRIORITY_HIGH ){
                highQ.push(_threads[i]);
            }
            else if(prio == VM_THREAD_PRIORITY_NORMAL ){
                medQ.push(_threads[i]);
            }
            else if(prio == VM_THREAD_PRIORITY_LOW){
                lowQ.push(_threads[i]);
            }
            else if( prio ==0){
                jamezQ.push(_threads[i]);
            }
            else{
                return VM_STATUS_FAILURE;
            }
        }
    }
    MachineResumeSignals(_signalstate);
    return VM_STATUS_SUCCESS;
  }

  void VMAlarmCallback(void* data){
    //printf("In VMAlarmCallback\n");
    _total_ticks++;
    
    VMScheduleThreads();
  }
  
  void VMScheduleThreads() {
    Thread *tmp = NULL;
    // First lets get the priority of the current thread.
    TVMThreadPriority prio;
    TVMThreadID cur;
    VMThreadID(&cur);
    Thread *cur_thread = getThreadByID(cur);
    if(cur_thread->getState() != VM_THREAD_STATE_RUNNING) {
      // It's not even running, lets activate the waiting thread.
      prio = 0;
    } else {
      prio = cur_thread->getPriority();
    }
    if(!highQ.empty() && prio < VM_THREAD_PRIORITY_HIGH) {
      printf("High Priority Found\n");
      tmp = highQ.front();
      highQ.pop();
    } else if (!medQ.empty() && prio < VM_THREAD_PRIORITY_NORMAL) {
      printf("Normal Priority Found\n");
      tmp = medQ.front();
      medQ.pop();
    } else if (!lowQ.empty() && prio < VM_THREAD_PRIORITY_LOW) {
      printf("Low Priority Found\n");
      tmp = lowQ.front();
      lowQ.pop();
    } else if (!jamezQ.empty() && prio == 0) {
      printf("JamezQ Priority Found\n");
      tmp = jamezQ.front();
      jamezQ.pop();
    }
    if(tmp != NULL) {
      tmp->run();
    }
  }  
  

}
