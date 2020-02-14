/* ------------------------------------------------------------------------
   phase1.c

   CSCV 452

   ------------------------------------------------------------------------ */
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <phase1.h>
#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
int sentinel (void *);
extern int start1 (char *);
void dispatcher(void);
void launch();
static void enableInterrupts();
static void check_deadlock();
static void insertRL(proc_ptr proc);
static void clear_process(proc_ptr process);

/* -------------------------- Globals ------------------------------------- */

/* Patrick's debugging global variable... */
int debugflag = 1;

/* the process table */
proc_struct ProcTable[MAXPROC];

/* Process lists  */
proc_ptr ReadyList;
//avoiding extra list and just looping through the process table for simplicity

/* current process ID */
proc_ptr Current;

/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;


/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
	     Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup(){
   int i;      /* loop index */
   int result; /* value returned by call to fork1() */

   /* initialize the process table */
   for (i = 0; i < MAXPROC; i++){
       clear_prcess(ProcTable[i]);
   }

   /* Initialize the Ready list, etc. */
   if (DEBUG && debugflag)
      console("startup(): initializing all lists\n");
   ReadyList = NULL;

   /* Initialize the clock interrupt handler */
   //int_vec[CLOCK_DEV] = clock_handler;

   /* startup a sentinel process */
   if (DEBUG && debugflag)
       console("startup(): calling fork1() for sentinel\n");
   result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                   SENTINELPRIORITY);
   if (result < 0) {
      if (DEBUG && debugflag)
         console("startup(): fork1 of sentinel returned error, halting...\n");
      halt(1);
   }

   /* start the test process */
   if (DEBUG && debugflag)
      console("startup(): calling fork1() for start1\n");
   result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
   if (result < 0) {
      console("startup(): fork1 for start1 returned an error, halting...\n");
      halt(1);
   }

   dispatcher();
   console("startup(): Should not see this message! ");
   console("Returned from fork1 call that created start1\n");

   return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish(){
   if (DEBUG && debugflag)
      console("in finish...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*f)(char *), char *arg, int stacksize, int priority){
    int proc_slot;
    bool is_sentinal = strcmp(name, "sentinel") == 0;

    if (DEBUG && debugflag)
      console("fork1(): creating process %s\n", name);

    /* test if in kernel mode; halt if in user mode */
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
     //not in kernel mode
     console("Kernel Error: Not in kernel mode, may not fork\n");
     halt(1);
    }

    //disable interrupts
    disableInterrupts();

    //check if arguments passed are NULL
    if (f == NULL || name == NULL ){
        if (DEBUG && debugflag)}{
            console("fork1(): Null pointer argument recieved\n");
        }
        return -1;
    }

    //check if function priority makes sense
    if ((!is_sentinal && (priority > MAXPRIORITY || priority < MINPRIORITY)){
        if (DEBUG && debugflag)}{
            console("fork1(): Priority out of bounds\n");
        }
        return -1;
    }

    /* Return if stack size is too small */
    if (stacksize < USLOSS_MIN_STACK){
        if (DEBUG && debugflag){
            console("fork1(): Stacksize smaller than USLOSS_MIN_STACK\n");
        }
       return -2;
    }

    /* find an empty slot in the process table */
    proc_slot = next_pid % MAXPROC;
    int pid_count = 0;
    while (pid_count < MAXPROC && ProcTable[proc_slot].status != NULL){
       next_pid++;
       proc_slot = next_pid % MAXPROC;
       pid_count++;
    }

    if (pid_count >= MAXPROC){
       if (DEBUG && debugflag){
           console("fork1(): process table full\n");
       }
       return -1;
    }

    /* fill-in entry in process table */
    if ( strlen(name) >= (MAXNAME - 1) ) {
        console("fork1(): Process name is too long.  Halting...\n");
        halt(1);
    }

    //store metadata about process
   strcpy(ProcTable[proc_slot].name, name);
   ProcTable[proc_slot].pid = next_pid++;
   ProcTable[proc_slot].priority = priority;
   ProcTable[proc_slot].status = READY;

   //Store function pointer and argument value
   ProcTable[proc_slot].start_func = f;
   if ( arg == NULL )
      ProcTable[proc_slot].start_arg[0] = '\0';
   else if ( strlen(arg) >= (MAXARG - 1) ) {
      console("fork1(): argument too long.  Halting...\n");
      halt(1);
   }
   else
      strcpy(ProcTable[proc_slot].start_arg, arg);

   //malloc the stack for the process
   ProcTable[proc_slot].stacksize = stacksize;
   ProcTable[proc_slot].stack = malloc(ProcTable[proc_slot].stacksize);

   //add parent pid for processes
   //add child pid for current
   if (Current){
       Current.num_kids++;
       ProcTable[proc_slot].parent_pid = Current.pid;
       Current.child_proc_ptr = &ProcTable[proc_slot];
   }

   /* Initialize context for this process, but use launch function pointer for
    * the initial value of the process's program counter (PC)
    */
   context_init(&(ProcTable[proc_slot].state), psr_get(),
                ProcTable[proc_slot].stack,
                ProcTable[proc_slot].stacksize, launch);

   //add process to ReadyList
   insertRL(ProcTable[proc_slot]);

   /* for future phase(s) */
   p1_fork(ProcTable[proc_slot].pid);

   //call dispatcher
   if (!is_sentinal) {
       dispatcher();
   }

   //enable interrupts for parent pid
   enableInterrupts();

   return ProcTable[proc_slot].pid;
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch(){
   int result;

   if (DEBUG && debugflag)
      console("launch(): started\n");

   /* Enable interrupts */
   enableInterrupts();

   /* Call the function passed to fork1, and capture its return value */
   result = Current->start_func(Current->start_arg);

   if (DEBUG && debugflag)
      console("Process %d returned to launch\n", Current->pid);

   quit(result);

} /* launch */


/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
		-1 if the process was zapped in the join
		-2 if the process has no children
   Side Effects - If no child process has quit before join is called, the
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *code){
    //disable interrupts
    disableInterrupts();

    //check if there are child processes
    if (Current.child_proc_ptr == NULL){
        enableInterrupts();
        return -2;
    }

    //check if the current process has been zapped
    if (Current.status == ZAPPED){
        enableInterrupts();
        return -1;
    }

    //check if children have quit
    for (i = 0; i < MAXPROC; i++){
        process = ProcTable[i];
        if(process.status == QUIT && process.parent_pid == Current.pid){
            *code = process.exit_code;
            enableInterrupts();
            return process.pid;
        }
        process = process.next_proc_ptr;
    }
} /* join */


/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int code){
    disableInterrupts();
    /* test if in kernel mode; halt if in user mode */
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
        //not in kernel mode
        console("Kernel Error: Not in kernel mode, may not quit\n");
        halt(1);
    }

    for(int i = 1; i < MAXPROC; i++){
        proc_ptr process = ProcTable[i];
        if(process.status != QUIT && process.parent_pid == Current.pid && process != Current) {
            console("quit(): process still has children");
            halt(1);
            return;
        }
    }

    //add quit metadata
    Current.status = QUIT;
    Current.exit_code = code;
    p1_quit(Current->pid);

    for(int i = 1; i < MAXPROC; i++){
        proc_ptr process = ProcTable[i];
        if(process.status == QUIT && process.parent_pid == Current.pid && process != Current) {
            clear_process(process);
        } else if(process.status == ZAPPED && process.zapped_pid == Current.pid){
            process.status = READY;
        }
    }

    //think dispatcher needs to be called?
    dispatcher();
} /* quit */


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void){
    //TODO
   proc_ptr next_process;

   p1_switch(Current->pid, next_process->pid);
} /* dispatcher */

/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
	     processes are blocked.  The other is to detect and report
	     simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
		   and halt.
   ----------------------------------------------------------------------- */
int sentinel (void * dummy){
   if (DEBUG && debugflag)
      console("sentinel(): called\n");
   while (1)
   {
      check_deadlock();
      waitint();
   }
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void check_deadlock(){
    int total_processes;
    int ready_processes;
    for(int i = 1; i < MAXPROC; i++){
        proc_ptr process = ProcTable[i];
        if(process.status == READY) {
            ready_processes++;
            total_processes++;
        } else if (ProcTable[i].status == BLOCKED || ProcTable[i].status == ZAPPED) {
            total_processes++;
        }
    }

    if (ready_processes == 1 ){
        if (total_processes != 1){ //processes stuck
            halt(1);
        } else { //not deadlock
            halt(0);
        }

    }
} /* check_deadlock */


/*
 * Disables the interrupts.
 */
void disableInterrupts(){
  /* turn the interrupts OFF iff we are in kernel mode */
  if((PSR_CURRENT_MODE & psr_get()) == 0) {
    //not in kernel mode
    console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
    halt(1);
  } else
    /* We ARE in kernel mode */
    psr_set( psr_get() & ~PSR_CURRENT_INT );
} /* disableInterrupts */

//end skeleton code

/*
 * enable the interrupts.
 */
void enableInterrupts(){
    /* turn the interrupts OFF iff we are in kernel mode */
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
      //not in kernel mode
      console("Kernel Error: Not in kernel mode, may not enable interrupts\n");
      halt(1);
    } else
      /* We ARE in kernel mode */
      psr_set( psr_get() & ~PSR_CURRENT_INT );
}


int zap(int pid){
    if (Current.pid == pid){
        console("zap(): process cannot zap itself");
        halt(1);
    }

    proc_ptr process;
    for(int i = 1; i < MAXPROC; i++){
        process = ProcTable[i];
        if (process.pid == pid){
            break;
        }
        process = NULL;
    }

    if (!process){
        console("zap(): Cannot zap a process that doesn't exist");
        halt(1);
    }

    Current.status = ZAPPED;
    Current.zapped_pid = process.pid;
}

int	is_zapped(void){
    return Current.status == ZAPPED;
}

int	getpid(void){
    return Current.pid;
}

void dump_processes(void){
    for(int i = 1; i < MAXPROC; i++){
        proc_ptr process = ProcTable[i];
        //TODO just printing process metadata
    }
}

void clock_handler(int pid){
    //TODO
    //DEFER UNTIL FORK1 JOIN QUIT AND DISPATCHER WORK
}

//SUPPORT FOR LATER PHASES (REQUIRED)

int block_me(int new_status){
    //TODO not sure about this one. it says to block the process but use new_status as status?
    if (new_status <= 10){
        console("block_me(): status not greater than 10\n");
        halt(1);
    }
    if (Current.status == ZAPPED){
        console("block_me(): attempting to block a zapped process\n");
        return -1;
    }
    Current.status = new_status;
    return 0;
}

int unblock_proc(int pid){
    if (Current.status == ZAPPED){
        console("unblock_proc(): attempting to unblock a zapped process\n");
        return -1;
    }

    for(int i = 1; i < MAXPROC; i++){
        proc_ptr process = ProcTable[i];
        if (process.pid == pid && process.pid != Current.pid && process.status > 10){
            process.status = READY;
            insertRL(process);
            return 0;
        }
    }
    console("unblock_proc(): attempting to unblock PID that does not exist/current process/status<=10\n");
    return -2;
}

int read_cur_start_time(void){
    //TODO
}

void time_slice(void){
    //TODO
}

//non-required helper functions

void clear_process(proc_ptr process){
    process.next_proc_ptr = NULL;
    process.child_proc_ptr = NULL;
    process.next_sibling_ptr = NULL;
    strcpy(process.name, "");   /
    strcpy(process.startArg, "");
    process.state = null;
    process.pid = -1;
    process.priority = -1;
    process.start_func = NULL;
    process.stack = NULL;
    process.stacksize = 1;
    process.status = -1;
    process.parent_pid = -1;
    process.exit_code = -1;
    process.num_kids = 0;
    process.start_time = -1;
}

/* ------------------------------------------------------------------------
   Name - insertRL
   *based on insertRL() from lecture slides
   Purpose - insert proc into target_list
   Parameters - proc, thr process being inserted
   Returns - nothing
   Side Effects - target_list is updated to contain proc
   ----------------------------------------------------------------------- */
static void insertRL(proc_ptr proc){
    proc_ptr walker, previous;  //pointers to PCB
    previous = NULL;
    walker = ReadyList;
    while (walker != NULL && walker->priority <= proc->priority) {
    	previous = walker;
    	walker = walker->next_proc_ptr
    }
    	if (previous == NULL) {
    		/* process goes at front of ReadyList */
    		proc->next_proc_ptr = ReadyList;
    		ReadyList = proc;
    	}else {
    		/* process goes after previous */
    		previous->next_proc_ptr = proc;
    		proc->next_proc_ptr = walker;
    	}
    	return;
}/* insertRL */
