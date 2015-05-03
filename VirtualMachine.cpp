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
  std::vector<Thread*> _sleeping_threads;
  TVMThreadID _ntid;
  long long unsigned int _largestprime;
  long long unsigned int _lastlargestprime;
  long long unsigned int _largesttest;
  std::queue<Thread*> highQ, medQ, lowQ, jamezQ;
//pronounced qwa
  TVMThreadID _current_thread;
  Thread *to_deletes[2]; // Threads to be deleted.
		      // Because a thread can't delete itself!
  
  TVMStatus VMThreadID(TVMThreadIDRef threadref);
  Thread *getThreadByID(TVMThreadID id);
  void timetokill(void* param);
  void VMScheduleThreads();
  TVMStatus VMThreadDelete(TVMThreadID thread);
  TVMStatus VMThreadTerminate(TVMThreadID thread);
  void skeleton(void* param);
  void mainSkel(void* param);
  TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref);

  
  struct skelArg{
    TVMThreadEntry entry;
    void* param;
  };
  
  struct mainArg{
    TVMMainEntry entry;
    int argc;
    char **argv;
  };
  
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
    TVMTick sleep;
    SMachineContextRef mcntxref; 
  public:
    Thread(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadID tid){
      sleep = 0;
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
    void setSleep(TVMTick sleep) {
      this->sleep = sleep;
    }
    TVMTick getSleep() {
      return sleep;
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
      if(x != NULL){
          // Only activate it if it was activated before.
          if(x->getState() == VM_THREAD_STATE_READY || 
             x->getState() == VM_THREAD_STATE_RUNNING) {
            // we set it to dead just so we can activate it immediatly.
	    x->setState(VM_THREAD_STATE_DEAD);
            VMThreadActivate(ref);
            
          }
          printf("Switched from thread %u to thread %u\n",ref,tThreadID);
          _current_thread = this->tThreadID;
          MachineContextSwitch(x->getRef(),this->mcntxref);
      }
      else{
	_current_thread = this->tThreadID;
	printf("Switched to thread %u\n", tThreadID);
	MachineContextRestore(this->mcntxref);
      }
    }
    SMachineContextRef getRef() {
      return mcntxref;
    }    
    ~Thread() {
      printf("IN Thread %u deconstructor\n",tThreadID);
      delete mcntxref;
      free(tStack);
      printf("Finished Thread %u deconstructor\n",tThreadID);
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
    to_deletes[0] = NULL; // Nothing to delete yet
    to_deletes[1] = NULL; // 
    
    printf("In VMStart\n");
    //initialize machine
    MachineInitialize(_machinetickms);
    MachineEnableSignals();
    
    //Create Thread for VM (ID = 0)
    //printf("Creating Main Thread: tid = %d\n",_ntid);
    VMThreadCreate(timetokill, NULL, 10, VM_THREAD_PRIORITY_LOW, &_ntid);
    

    //create dummy thread (ID = 1)
    VMThreadCreate(timetokill, NULL, 0x100000, 000, &_ntid); //was informed we need at least 10k
    VMThreadActivate(1);
    
    //printf("Thread %d activated\n",(_ntid-1));
    //load and call module
    module_main = VMLoadModule(argv[0]);
    if(module_main == NULL)
      return VM_STATUS_FAILURE;
    struct mainArg *main = new struct mainArg;
    main->entry = module_main;
    main->argc = argc;
    main->argv = argv;
    
    VMThreadCreate(mainSkel, main, 0x100000, VM_THREAD_PRIORITY_NORMAL, &_ntid);
    VMThreadActivate(_ntid-1);
    
    // We do it last to make sure it doesn't run before we activate.
    MachineRequestAlarm(alarmtick, VMAlarmCallback, NULL);
    VMScheduleThreads(); // Main stops excuting here, once it starts
			 // again (by switching back to
			 // the main thread). The machine exits.
    printf("Exiting VMStart.\n");
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMFileWrite(int filedescriptor, void *data, int *length) {
    if(data == NULL || length == NULL)
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    int what_is_left;
    printf("###: \n");
    what_is_left = write(filedescriptor,(char *)data,*length);
    printf("###: \n");
    if(what_is_left == -1)
      return VM_STATUS_FAILURE;
    *length = what_is_left; // This is how we tell who called us how much we
                                     // actually wrote.
    return VM_STATUS_SUCCESS;
  }
  
  TVMStatus VMThreadSleep(TVMTick tick) {
    MachineSuspendSignals(_signalstate);
    TVMThreadID cur;
    VMThreadID(&cur);
    printf("Thread %u is tired and needs to sleep for %u ticks.\n",cur,tick);
    Thread *x = getThreadByID(cur);
    x->setState(VM_THREAD_STATE_WAITING);
    x->setSleep(tick);
    _sleeping_threads.push_back(x);
    MachineResumeSignals(_signalstate);
    VMScheduleThreads();
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
    printf("In VMThreadCreate, tid = %u\n",_ntid);
    MachineSuspendSignals(_signalstate);
    Thread *thread;
    if(_threads.size() >= 2){ //not VMMain or time to kill
        struct skelArg *newParam = new struct skelArg;
        newParam->entry = entry;
        newParam->param = param;
        thread = new Thread(skeleton,newParam,memsize,prio,_ntid);
    }
    else{
        thread = new Thread(entry,param,memsize,prio,_ntid);
    }
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
  
  void skeleton(void* param){
    struct skelArg *args = (struct skelArg*) param;
    args->entry(args->param);
    delete args;
    TVMThreadID cur;
    VMThreadID(&cur);
    VMThreadTerminate(cur);
    VMThreadDelete(cur);
    printf("In skeleton, finished deleting thread %u\n",cur);
    VMScheduleThreads();
  }
  
  void mainSkel(void* param){
      struct mainArg *args = (struct mainArg*) param;
      args->entry(args->argc,args->argv);
      delete args;
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
    // Lets deal with sleepy sheepys
    TVMTick tmp;
    for(int i = _sleeping_threads.size() - 1; i >= 0; --i) {
      tmp = _sleeping_threads[i]->getSleep();
      if(tmp == 0) {
	_sleeping_threads[i]->setState(VM_THREAD_STATE_DEAD);
	printf("Waking thread %u from it's nap\n",_sleeping_threads[i]->getID());
	VMThreadActivate(_sleeping_threads[i]->getID());
	_sleeping_threads.erase(_sleeping_threads.begin()+i);
      } else {
	_sleeping_threads[i]->setSleep(tmp - 1);
      }
    }
    _total_ticks++;
    if(_threads.size() == 2) {
      // We have nothing left to do but to terminate.
      // We terminate by letting VMMain finish.
      printf("We are only killing time now... might as well die.\n");
      VMThreadActivate(0);
    }
    VMScheduleThreads();
  }
  
  void VMScheduleThreads() {
    Thread *tmp = NULL;
    MachineSuspendSignals(_signalstate);
    // First lets get the priority of the current thread.
    TVMThreadPriority prio;
    TVMThreadID cur;
    VMThreadID(&cur);
    Thread *cur_thread = getThreadByID(cur);
    if(to_deletes[0] != NULL) {
      if(cur != to_deletes[0]->getID()) {
	printf("Found a thread to cleanup, cleaning thread %u\n",to_deletes[0]->getID());
	delete to_deletes[0];
	to_deletes[0] = NULL;
      }
    }
    if(to_deletes[1] != NULL) {
      if(cur != to_deletes[1]->getID()) {
	printf("Found a thread to cleanup, cleaning thread %u\n",to_deletes[1]->getID());
	delete to_deletes[1];
	to_deletes[1] = NULL;
      }
    }
    if(cur_thread == NULL){
        prio = 0;
    }
    else if(cur_thread->getState() != VM_THREAD_STATE_RUNNING) {
      // It's not even running, lets activate the waiting thread.
      prio = 0;
    } else {
      prio = cur_thread->getPriority();
    }
    if(!highQ.empty() && prio <= VM_THREAD_PRIORITY_HIGH) {
      printf("High Priority Found\n");
      tmp = highQ.front();
      highQ.pop();
    } else if (!medQ.empty() && prio <= VM_THREAD_PRIORITY_NORMAL) {
      printf("Normal Priority Found\n");
      tmp = medQ.front();
      medQ.pop();
    } else if (!lowQ.empty() && prio <= VM_THREAD_PRIORITY_LOW) {
      printf("Low Priority Found\n");
      tmp = lowQ.front();
      lowQ.pop();
    } else if (!jamezQ.empty() && prio == 0) {
      printf("JamezQ Priority Found\n");
      tmp = jamezQ.front();
      jamezQ.pop();
    }
    MachineResumeSignals(_signalstate);
    if(tmp != NULL) {
      tmp->run();
    }
    
  }  
  
  TVMStatus VMThreadTerminate(TVMThreadID thread){
      //Deal with mutexes
      printf("Terminating thread %u\n",thread);
      MachineSuspendSignals(_signalstate);
      Thread *tmp = getThreadByID(thread);  
      if(tmp == NULL){
          MachineResumeSignals(_signalstate);
          return VM_STATUS_ERROR_INVALID_ID;
      }
      if(tmp->getState() == VM_THREAD_STATE_DEAD){
          MachineResumeSignals(_signalstate);
          return VM_STATUS_ERROR_INVALID_STATE;          
      }
      tmp->setState(VM_THREAD_STATE_DEAD);
      MachineResumeSignals(_signalstate);
      return VM_STATUS_SUCCESS;
  }
  
  TVMStatus VMThreadDelete(TVMThreadID thread){
      MachineSuspendSignals(_signalstate);
      printf("Deleting thread %u\n",thread);
      Thread *tmp;
      for(unsigned int i = 0; i < _threads.size(); ++i) {
        if(_threads[i]->getID() == thread) {
          tmp = _threads[i];
          _threads.erase(_threads.begin()+i);
	  if(to_deletes[0] == NULL) {
	    to_deletes[0] = tmp;
	  } else if(to_deletes[1] == NULL) {
	    to_deletes[1] = tmp;
	  } else {
	    printf("Major issue, no place to delete this thread!\n");
	  }
        }
      }
      printf("Thread %u deleted.\n", thread);
      MachineResumeSignals(_signalstate);
      
      return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
      MachineSuspendSignals(_signalstate);
      Thread *tmp = getThreadByID(thread);  
      if(tmp == NULL){
          MachineResumeSignals(_signalstate);
          return VM_STATUS_ERROR_INVALID_ID;
      }
      if(stateref == NULL){
          MachineResumeSignals(_signalstate);
          return VM_STATUS_ERROR_INVALID_PARAMETER;          
      }
      *stateref = tmp->getState();
      
      return VM_STATUS_SUCCESS;
  }

}
