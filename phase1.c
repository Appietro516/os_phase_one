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
void enableInterrupts();
void disableInterrupts();
void dump_process(proc_struct);
void clock_handler(int pid);

static void check_deadlock();
static void insert_into_ready_list(proc_ptr proc);
static void remove_from_ready_list(proc_ptr proc);
static void clear_process(proc_ptr process);

/* ------------------------- MACROS ----------------------------------- */
#define READY 1
#define BLOCKED 2
#define ZAPPED 3
#define QUIT 4
#define EMPTY -1
#define TIME_SLICE 8000

/* -------------------------- Globals ------------------------------------- */

/* Patrick's debugging global variable... */
int debugflag = 1;

/* the process table */
proc_struct ProcTable[MAXPROC];

/* Process lists  */
proc_ptr ReadyList;

/* current process ID */
proc_ptr Current;

/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;

/**
 *  Bootstrapping function called and required by USLOSS lib
 */
void startup() {
    // value returned by call to fork1()
    int result;

    // initialize the process table
    for (int i = 0; i < MAXPROC; i++){
        clear_process(&ProcTable[i]);
    }

    // Initialize the Ready list, etc.
    ReadyList = NULL;

    // Initialize the clock interrupt handler
    int_vec[CLOCK_INT] = clock_handler;

    // Start the sentinel process
    result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK, SENTINELPRIORITY);

    if (result < 0) {
        if (DEBUG && debugflag)
            console("startup(): fork1 of sentinel returned error, halting...\n");
        halt(1);
    }

    // Start the test process
    result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);

    if (result < 0) {
        console("startup(): fork1 for start1 returned an error, halting...\n");
        halt(1);
    }

    console("startup(): Should not see this message! ");
    console("Returned from fork1 call that created start1\n");

    return;
}

/**
 *  Finish function required by the USLOSS lib
 */
void finish(){
   if (DEBUG && debugflag)
      console("in finish...\n");
}

/**
 *          Name -> fork1
 *       Purpose -> Gets a new process from the process table and initializes
 *                  information of the process.  Updates information in the
 *                  parent process to reflect this child process creation.
 *    Parameters -> The process procedure address, the size of the stack and
 *                  the priority to be assigned to the child process.
 *       Returns -> The process id of the created child or -1 if no child could
 *                  be created or if priority is not between max and min priority.
 *  Side Effects -> ReadyList is changed, ProcTable is changed, Current
 *                  process information changed
 */
int fork1(char *name, int (*f)(char *), char *arg, int stacksize, int priority) {
    int proc_slot;
    int is_sentinel = strcmp(name, "sentinel") == 0;

    if (DEBUG && debugflag){
      console("fork1(): creating process %s\n", name);
    }

    /* test if in kernel mode; halt if in user mode */
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
        //not in kernel mode
        console("Kernel Error: Not in kernel mode, may not fork\n");
        halt(1);
    }

    //check if arguments passed are NULL
    if (f == NULL || name == NULL ){
        if (DEBUG && debugflag){
            console("fork1(): Null pointer argument recieved\n");
        }
        return -1;
    }

    //check if function priority makes sense
    if (!is_sentinel && (priority < MAXPRIORITY || priority > MINPRIORITY)) {
        if (DEBUG && debugflag){
            //printf("priority:%d\nmaxprior:%d\nminprior:%d/n", priority, MAXPRIORITY, MINPRIORITY);
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

    // console("fork1(): REACHED ERROR CHECKS\n");

    /* find an empty slot in the process table */
    proc_slot = next_pid % MAXPROC;
    int pid_count = 0;
    while (pid_count < MAXPROC && ProcTable[proc_slot].status != EMPTY){
        next_pid++;
        proc_slot = next_pid % MAXPROC;
        pid_count++;
    }

    // console("fork1(): GOT PIDS\n");

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

    // console("fork1(): GOT PNAME\n");

    // Store metadata about process
    strcpy(ProcTable[proc_slot].name, name);
    ProcTable[proc_slot].pid = next_pid++;
    ProcTable[proc_slot].priority = priority;
    ProcTable[proc_slot].status = READY;

    // Store function pointer and argument value
    ProcTable[proc_slot].start_func = f;
    if ( arg == NULL ) {
        ProcTable[proc_slot].start_arg[0] = '\0';
    }
    else if ( strlen(arg) >= (MAXARG - 1) ) {
        console("fork1(): argument too long.  Halting...\n");
        halt(1);
    }
    else {
        strcpy(ProcTable[proc_slot].start_arg, arg);
    }

    //malloc the stack for the process
    ProcTable[proc_slot].stacksize = stacksize;
    ProcTable[proc_slot].stack = malloc(ProcTable[proc_slot].stacksize);

    // Add parent pid for processes
    // Add child ptr to list of children
    if (Current) {
        Current->num_kids++;
        ProcTable[proc_slot].next_sibling_ptr = Current->child_proc_ptr;
        Current->child_proc_ptr = &ProcTable[proc_slot];
    }

    ProcTable[proc_slot].parent = Current;

//    console("fork1(): GOT CONTEXT INIT\n");

   /* Initialize context for this process, but use launch function pointer for
    * the initial value of the process's program counter (PC)
    */
   context_init(&(ProcTable[proc_slot].state), psr_get(),
                ProcTable[proc_slot].stack,
                ProcTable[proc_slot].stacksize, launch);

   //add process to ReadyList
   insert_into_ready_list(&ProcTable[proc_slot]);


   /* for future phase(s) */
   p1_fork(ProcTable[proc_slot].pid);

   //call dispatcher
   if (!is_sentinel) {
       dispatcher();
   }

//    console("fork1(): PASSED DISPATCHER\n");

   return ProcTable[proc_slot].pid;
}


/**
 *          Name -> join
 *       Purpose -> Wait for a child process (if one has been forked) to quit.  If
 *                  one has already quit, don't wait.
 *                  parent process to reflect this child process creation.
 *    Parameters -> A pointer to an int where the termination code of the
 *                  quitting process is to be stored.
 *       Returns -> The process id of the quitting child joined on.
 *	                -1 if the process was zapped in the join
 *                  -2 if the process has no children
 *  Side Effects -> If no child process has quit before join is called, the
 *                  parent is removed from the ready list and blocked.
 */
int join(int *code){
    // // Disable interrupts
    // disableInterrupts();

    // Check if there are child processes
    if (Current->child_proc_ptr == NULL){
        //enableInterrupts();
        return -2;
    }

    // Check if the current process has been zapped
    if (Current->status == ZAPPED){
        //enableInterrupts();
        return -1;
    }

    int can_terminate = 0;
    proc_ptr child = Current->child_proc_ptr;
    // check if children have quit
    while (child != NULL) {
        if (child->status == QUIT) {
            can_terminate = 1;
            break;
        }
        child = child->next_sibling_ptr;
    }

    if (can_terminate == 0) {
        // No children have quit, set to blocked, remove from ready list
        Current->status = BLOCKED;
        remove_from_ready_list(Current);
        dispatcher();

        // A child has quit
        // console("A child has quit, this process has gained focus once more.");
        child = Current->child_proc_ptr;
        while (child != NULL) {
            if (child->status == QUIT) {
                can_terminate = 1;
                break;
            }
            child = child->next_sibling_ptr;
        }
    }

    *code = child->quit_status;
    child->pid;

    //enableInterrupts();
    return child->pid;
}


/**
 *          Name -> quit
 *       Purpose -> Stops the child process and notifies the parent of the death by
 *                  putting child quit info on the parents child completion code
 *                  list.
 *    Parameters -> The code to return to the grieving parent
 *       Returns -> None.
 *  Side Effects -> Changes the parent of pid child completion status list.
 */
void quit(int code) {
    if (DEBUG && debugflag){
        printf("quit(): PERFORMING QUIT FOR %d\n", Current->pid);
    }

    //Test if in kernel mode, halt if in user mode
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
        // Not in kernel mode
        console("Kernel Error: Not in kernel mode, may not quit\n");
    }

    // Check if process has children
    proc_ptr child = Current->child_proc_ptr;
    while (child != NULL) {
        if (child->status != QUIT) {
            console("Process with children may not quit.\n");
            halt(1);
            //may have to waitint() here, not sure. She just said wait to quit in project notes
        }
        child = child->next_sibling_ptr;
    }

    // Add quit metadata
    Current->status = QUIT;
    Current->quit_status = code;

    // Check if parent is waiting for join
    proc_ptr parent = Current->parent;
    if (parent != NULL && parent->status == BLOCKED) {
        parent->status = READY;
        insert_into_ready_list(parent);
    }

    //get processes with this quitting process as a zapped_pid and Ready them up
    for(int i = 0; i < MAXPROC; i++){
        proc_ptr process = &ProcTable[i];
        //process is ZAPPED, process had zapped the quiting process
        if(process->status == ZAPPED && process->zapped_pid == Current->pid) {
            process->status = READY;
            process->zapped_pid = -1;
            insert_into_ready_list(process);
        }
    }
    dispatcher();

    console("SHOUDNT BE HERE\n");
    halt(1);



    // for(int i = 1; i < MAXPROC; i++){
    //     proc_ptr process = &ProcTable[i];
    //     if(process->status != QUIT && process->parent->pid == Current->pid && process != Current) {
    //         enableInterrupts();
    //         console("quit(): process still has children\n");
    //         while (1){
    //            check_deadlock();
    //            waitint();
    //         }
    //     }
    // }

    // p1_quit(Current->pid);

    // //should remove from ready list, cear_process on current, then call dispsatcher
    // //should also check for ZAPPED process and add to READY

    // //this code does not appear to be working
    // // for(int i = 1; i < MAXPROC; i++){
    // //     proc_struct process = ProcTable[i];
    // //     if(process.status == QUIT && process.parent_pid == Current->pid && &process != Current) {
    // //         console("GOT QUIT");
    // //         clear_process(&process);
    // //     } else if(process.status == ZAPPED && process.zapped_pid == Current->pid){
    // //         process.status = READY;
    // //     }
    // // }
}

int zap(int pid){
    //make sure process is not zapping itself
    if (Current->pid == pid){
        console("zap(): process cannot zap itself");
        halt(1);
    }

    //get ptr to zapee
    proc_ptr process;
    for(int i = 1; i < MAXPROC; i++){
        process = &ProcTable[i];
        if (process->pid == pid){
            break;
        }
        process = NULL;
    }

    //null ptr means zapee doesnt exist
    if (!process){
        console("zap(): Cannot zap a process that doesn't exist");
        halt(1);
    }

    //per phase 1 zap return values "The calling process itself was zapped"
    if (Current->status == ZAPPED){
        console("Calling process itself was zapped")
        return -1;
    }


    //set current to ZAPPED, remove from RL, call dispatcher
    Current->status = ZAPPED;
    Current->zapped_pid = process->pid;
    remove_from_ready_list(Current);
    dispatcher();

    return 0;
}

int	is_zapped(void){
    return Current->status == ZAPPED;
}


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
void dispatcher(void) {
    // proc_ptr p = ReadyList;
    // while(p!=NULL){
    //     dump_process(*p);
    //     p = p->next_proc_ptr;
    // }

    proc_ptr process_to_schedule, previous;
    previous = Current;
    process_to_schedule = ReadyList;
    Current = process_to_schedule;

    if (previous == NULL) {
        p1_switch(NULL, Current->pid);
        context_switch(NULL, &(Current->state));
    } else {
        p1_switch(previous->pid, Current->pid);
        context_switch(&(previous->state), &(Current->state));
    }

    // proc_ptr curr = ReadyList;
    // proc_ptr to_sched = curr;
    // while (curr != NULL) {
    //     if ((curr != to_sched && to_sched->run_time > TIME_SLICE) || curr->priority < to_sched->priority) {
    //         to_sched->run_time = 0;
    //         to_sched = curr;
    //     }
    //     curr = curr->next_proc_ptr;
    // }

    // to_sched->start_time = clock();

}

static void remove_from_ready_list(proc_ptr proc) {
    proc_ptr current, previous;
    previous = NULL;
    current = ReadyList;
    while(current != NULL) {
        if (current->pid == proc->pid) {
            if (previous == NULL) {
                ReadyList = current->next_proc_ptr;
            } else {
                previous->next_proc_ptr = current->next_proc_ptr;
            }
            return;
        }
        previous = current;
        current = current->next_proc_ptr;
    }

    console("Attempt to remove process that didn't exist in ReadyList.");
    halt(0);
}

static void insert_into_ready_list(proc_ptr proc) {
    proc_ptr current, previous;
    previous = NULL;
    current = ReadyList;
    while (current != NULL && current->priority <= proc->priority) {
    	previous = current;
    	current = current->next_proc_ptr;
    }
    if (previous == NULL) {
        // process goes at front of ReadyList
        proc->next_proc_ptr = ReadyList;
        ReadyList = proc;
    } else {
        // process goes after previous
        previous->next_proc_ptr = proc;
        proc->next_proc_ptr = current;
    }

    return;
}


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
}

/* check to determine if deadlock has occurred... */
static void check_deadlock() {
    int total_processes;
    int ready_processes;
    for(int i = 1; i < MAXPROC; i++){
        proc_struct process = ProcTable[i];
        if(process.status == READY) {
            ready_processes++;
            total_processes++;
        } else if (ProcTable[i].status == BLOCKED || ProcTable[i].status == ZAPPED) {
            total_processes++;
        }
    }

    if (ready_processes == 1 ){
        if (total_processes != 1){ // processes stuck
            console("Process terminated abnormally");
            halt(1);
        } else { //not deadlock
            console("All processes completed");
            halt(0);
        }
    }
}


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



   /* Call the function passed to fork1, and capture its return value */
   result = Current->start_func(Current->start_arg);

   if (DEBUG && debugflag)
      console("Process %d returned to launch\n", Current->pid);

   quit(result);
}


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
}

/*
 * Enable the interrupts.
 */
void enableInterrupts(){
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
      // Not in kernel mode
      console("Kernel Error: Not in kernel mode, may not disable interrupts\n");
      halt(1);
    } else
      // We ARE in kernel mode
     psr_set( psr_get() & PSR_CURRENT_INT );
}


void clock_handler(int pid){
    Current->run_time = clock() - Current->start_time;
    if (Current->run_time > TIME_SLICE) {
        dispatcher();
    }
}


/* ------------------------- Helper Functions ----------------------------------- */

void clear_process(proc_ptr process) {
    process->next_proc_ptr = NULL;
    process->child_proc_ptr = NULL;
    process->next_sibling_ptr = NULL;
    process->parent = NULL;
    strcpy(process->name, "");
    strcpy(process->start_arg, "");
    //process->state = NULL;
    process->pid = -1;
    process->parent_pid = -1;
    process->priority = -1;
    process->start_func = NULL;
    process->stack = NULL;
    process->stacksize = 1;
    process->status = -1;
    process->quit_status = -1;
    process->num_kids = 0;
    process->start_time = -1;
}

void dump_process(proc_struct process) {
    printf("-------------------------------\n");
    printf("Name: %s\n", process.name);
    printf("PID: %d\n", process.pid);
    printf("Parent's PID: %d\n", process.parent_pid);
    printf("Priority: %d\n", process.priority);
    printf("Status: %d\n", process.status);
    printf("Children count: %d\n", process.num_kids);
    printf("CPU time consumed: %d\n", process.run_time);
    printf("-------------------------------\n");
}

void dump_processes() {
    for (int i = 0; i < MAXPROC; i++) {
        proc_struct p = ProcTable[i];
        if (p.pid >= 0) {
            dump_process(ProcTable[i]);
        }
    }
}


/* ------------------------- TODO: Support for l8r phases ----------------------------------- */

int block_me(int new_status){
    // TODO not sure about this one. it says to block the process but use new_status as status?
    if (new_status <= 10){
        console("block_me(): status not greater than 10\n");
        halt(1);
    }
    if (Current->status == ZAPPED){
        console("block_me(): attempting to block a zapped process\n");
        return -1;
    }
    Current->status = new_status;
    return 0;
}

int unblock_proc(int pid){
    if (Current->status == ZAPPED){
        console("unblock_proc(): attempting to unblock a zapped process\n");
        return -1;
    }

    for(int i = 1; i < MAXPROC; i++){
        proc_struct proc = ProcTable[i];
        if (proc.pid == pid && proc.pid != Current->pid && proc.status > 10){
            proc.status = READY;
            insert_into_ready_list(&proc);
            return 0;
        }
    }
    console("unblock_proc(): attempting to unblock PID that does not exist/current process/status<=10\n");
    return -2;
}

int read_cur_start_time(void){
    if (Current){
        return Current->start_time;
    }
    return -1;

}

void time_slice(void){
    // TODO
}
