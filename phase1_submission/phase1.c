/* ------------------------------------------------------------------------
   phase1.c

   Anthony Pietrofeso
   Bryce Gallion

   2/23/2020

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
void (*intvec[NUM_INTS])(int dev, void*unit);

/* ------------------------- Macros ----------------------------------- */
#define EMPTY -1
#define RUNNING 1
#define READY 2
#define JOIN_BLOCKED 3
#define ZAPPED 4
#define QUIT 5
#define TIME_SLICE 8000

/* -------------------------- Globals ------------------------------------- */

/* Patrick's debugging global variable... */
int debugflag = 0;

/* the process table */
proc_struct ProcTable[MAXPROC];

/* Process lists  */
proc_ptr ReadyList;

/* current process ID */
proc_ptr Current;

/* the next pid to be assigned */
unsigned int next_pid = SENTINELPID;


/**
 *          Name -> finish
 *       Purpose -> required by USLOSS/ initializes startup variables,
                    clears the process table
 *    Parameters -> none
 *       Returns -> none
 *  Side Effects -> none
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

    console("startup(): Should not see this message! \n");
    console("Returned from fork1 call that created start1\n");

    return;
}


/**
 *          Name -> finish
 *       Purpose -> required by USLOSS
 *    Parameters -> none
 *       Returns -> none
 *  Side Effects -> none
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

    /* find an empty slot in the process table */
    proc_slot = next_pid % MAXPROC;
    int pid_count = 0;
    while (pid_count < MAXPROC && ProcTable[proc_slot].status != EMPTY){
        next_pid++;
        proc_slot = next_pid % MAXPROC;
        pid_count++;
    }

    //check if process table is full
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
        ProcTable[proc_slot].parent_pid = Current->pid;
        Current->num_kids++;
        ProcTable[proc_slot].next_sibling_ptr = Current->child_proc_ptr;
        Current->child_proc_ptr = &ProcTable[proc_slot];
    }

    ProcTable[proc_slot].parent = Current;


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
    // Check if there are child processes
    if (Current->child_proc_ptr == NULL && Current->quit_child_ptr == NULL){
        return -2;
    }

    // Check if the current process has been zapped
    for(int i = 1; i < MAXPROC; i++){
        proc_struct process = ProcTable[i];
        if(process.status != EMPTY){
            if (process.zapped_pid == Current->pid){
                return -1;
            }
        }
    }

    proc_ptr quit_child = Current->quit_child_ptr;
    if (quit_child == NULL) {
        // No children have quit, set to blocked, remove from ready list
        Current->status = JOIN_BLOCKED;
        remove_from_ready_list(Current);
        dispatcher();

        // This process has regained control because a child has quit
        quit_child = Current->quit_child_ptr;
    }

    *code = quit_child->quit_status;
    int quit_child_pid = quit_child->pid;
    Current->quit_child_ptr = quit_child->next_quit_sibling_ptr;
    Current->num_kids--;
    clear_process(quit_child);

    enableInterrupts();
    return quit_child_pid;
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

    // Test if in kernel mode, halt if in user mode
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
        // Not in kernel mode
        console("Kernel Error: Not in kernel mode, may not quit\n");
        halt(1);
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

    remove_from_ready_list(Current);

    // Add quit metadata
    Current->status = QUIT;
    Current->quit_status = code;

    // Check if parent is waiting for join
    proc_ptr parent = Current->parent;

    if (parent != NULL) {
        if (parent->status == JOIN_BLOCKED) {
            parent->status = READY;
            insert_into_ready_list(parent);
        }

        // Add current process to the list of QUIT children
        proc_ptr quit_child = parent->quit_child_ptr;
        if (quit_child == NULL) {
            parent->quit_child_ptr = Current;
            Current->next_quit_sibling_ptr = NULL;
        } else {
            while (quit_child->next_quit_sibling_ptr != NULL) {
                quit_child = quit_child->next_quit_sibling_ptr;
            }
            quit_child->next_quit_sibling_ptr = Current;
        }

        // Remove current process from normal list of childrean
        proc_ptr prev_child, curr_child;
        prev_child = NULL;
        curr_child = parent->child_proc_ptr;
        while (curr_child != NULL) {
            if (curr_child->pid == Current->pid) {
                if (prev_child == NULL) {
                    parent->child_proc_ptr = NULL;
                } else {
                    prev_child->next_quit_sibling_ptr = curr_child->next_sibling_ptr;
                }
            }
            prev_child = curr_child;
            curr_child = curr_child->next_sibling_ptr;
        }
    }

    //get processes with this quitting process as a zapped_pid and Ready them up
    for(int i = 0; i < MAXPROC; i++){
        proc_ptr process = &ProcTable[i];

        //process is ZAPPED, process had zapped the quiting process
        if(process->zapped_pid == Current->pid) {
            process->status = READY;
            process->zapped_pid = -1;
            insert_into_ready_list(process);
        }
    }

    dispatcher();
}


/**
 *          Name -> zap
 *       Purpose -> Blocks a process from running until the zapped process quits
 *                  parent process to reflect this child process creation.
 *    Parameters -> pid - id of process to be zapped
 *       Returns -> The status of the proces zapping. -1 if a blocked pid is zapped, 0 if there are no issues
 *  Side Effects -> The status of the zapper and zapee are changed in the process table
 */
int zap(int pid){
    //make sure process is not zapping itself
    if (Current->pid == pid){
        console("zap(): process cannot zap itself\n");
        halt(1);
    }

    //get ptr to zapped process
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
        console("zap(): Cannot zap a process that doesn't exist\n");
        halt(1);
    }

    if (Current->status == ZAPPED){
        return 0;
    }

    //set current to ZAPPED, remove from RL, call dispatcher
    process->status = ZAPPED;
    Current->zapped_pid = process->pid;
    remove_from_ready_list(Current);
    dispatcher();
    enableInterrupts();

    // Check if the current process has been zapped
    for(int i = 1; i < MAXPROC; i++){
        proc_struct process2 = ProcTable[i];
        if(process2.status != EMPTY){
            if (process2.zapped_pid != -1){
                return -1;
            }
        }
    }

    return 0;
}


/* ------------------------------------------------------------------------
   Name -  is_zapped
   Purpose - check if Current process is zapped
   Parameters - none
   Returns - 1 if zapped, 0 if not zapped
   Side Effects - none
   ----------------------------------------------------------------------- */
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
    proc_ptr process_to_schedule, previous;
    previous = Current;

    //record runtime for last process
    if (Current && Current->start_time != -1){
        Current->total_run_time = Current->total_run_time + sys_clock() - Current->start_time;
    }

    //schedule new process based on TIME_SLICE
    process_to_schedule = ReadyList;
    if ((process_to_schedule->current_run_time > TIME_SLICE) ) {
        process_to_schedule = ReadyList->next_proc_ptr;
    }
    Current = process_to_schedule;
    if (Current->status != ZAPPED){
        Current->status = RUNNING;
    }

    //set start time
    Current->start_time = sys_clock();

    //usloss switch context
    if (previous == NULL) {
        p1_switch(NULL, Current->pid);
        context_switch(NULL, &(Current->state));
    } else {
        p1_switch(previous->pid, Current->pid);
        context_switch(&(previous->state), &(Current->state));
    }
}


/**
 *          Name -> remove_from_ready_list
 *       Purpose -> remove a process from the list of ready processes
 *                  based on the provided insertRL function.
 *    Parameters -> proc_ptr proc - a pointer to the process to remove
 *       Returns -> none
 *  Side Effects -> A process will be removed from the ready list.
 */
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
}


/**
 *          Name -> insert_from_ready_list
 *       Purpose -> add a process to the ready list
                    based on provided insertRL function.
 *    Parameters -> proc_ptr proc - a pointer to the process to add
 *       Returns -> none
 *  Side Effects -> A process will be added into the ready list.
 */
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


/**
 *          Name -> check_deadlock
 *       Purpose -> checkts to determine if deadlock occured.
 *                  if the sentinal is running with other "active"
 *                  processes, deadlock has occured.
 *    Parameters -> none
 *       Returns -> none
 *  Side Effects -> none
 */
static void check_deadlock() {
    int ready_processes;
    for(int i = 1; i < MAXPROC; i++){
        proc_struct process = ProcTable[i];
        if(process.status == RUNNING || process.status == ZAPPED) {
            ready_processes++;
        }
    }

    if (ready_processes > 1) {
        console("Sentinel detected deadlock. Halting...\n");
        halt(1);
    } else { // not deadlock
        console("All processes completed.\n");
        halt(0);
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

   enableInterrupts();
   /* Call the function passed to fork1, and capture its return value */
   result = Current->start_func(Current->start_arg);
   disableInterrupts();

   if (DEBUG && debugflag)
      console("Process %d returned to launch\n", Current->pid);

   quit(result);
}


/**
 *          Name -> disableInterrupts
 *       Purpose -> disable USLOSS interrupts
 *    Parameters -> none
 *       Returns -> none
 *  Side Effects -> usloss will no longer send interrupts, preventing clock interrupts for phase 1
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


/**
 *          Name -> enableInterrupts
 *       Purpose -> enable USLOSS interrupts
 *    Parameters -> none
 *       Returns -> none
 *  Side Effects -> usloss will send interrupts, which could cause race conditions
 */
void enableInterrupts(){
    if((PSR_CURRENT_MODE & psr_get()) == 0) {
      // Not in kernel mode
      console("Kernel Error: Not in kernel mode, may not enable interrupts\n");
      halt(1);
    } else
      // We ARE in kernel mode
     psr_set( psr_get() | PSR_CURRENT_INT );
}


/**
 *          Name -> clock_handler
 *       Purpose -> runs on a USLOSS clock interrupt to call dispatcher if a process exceeds timeslice
 *    Parameters -> pid of currently running process
 *       Returns -> none
 *  Side Effects -> If a process has exceeded its time slice, will call dispatcher
 */
void clock_handler(int pid){
    Current->current_run_time = sys_clock() - Current->start_time;
    if (Current->current_run_time > TIME_SLICE) {
        dispatcher();
    }
}


/* ------------------------- Helper Functions ----------------------------------- */


/**
 *          Name -> clear_process
 *       Purpose -> free up a slot in process table by removing associated metadata
 *    Parameters -> proc_ptr process - pointer to processs to remove
 *       Returns -> none
 *  Side Effects -> empty slot in process table
 */
void clear_process(proc_ptr process) {
    process->next_proc_ptr = NULL;
    process->child_proc_ptr = NULL;
    process->next_sibling_ptr = NULL;
    process->parent = NULL;
    process->quit_child_ptr = NULL;
    process->next_quit_sibling_ptr = NULL;
    strcpy(process->name, "");
    strcpy(process->start_arg, "");
    process->pid = -1;
    process->parent_pid = -1;
    process->priority = -1;
    process->start_func = NULL;
    process->stack = NULL;
    process->stacksize = 1;
    process->status = -1;
    process->quit_status = -1;
    process->num_kids = 0;
    process->current_run_time = -1;
    process->total_run_time = -1;
    process->start_time = -1;
    process->zapped_pid = -1;
}


/**
 *          Name -> dump_process
 *       Purpose -> dump information related to a single process
 *    Parameters -> proc_ptr process - pointer to processs to remove
 *       Returns -> none
 *  Side Effects -> none
 */
void dump_process(proc_struct process) {
    printf("| %5d |", process.pid);
    printf("  %5d |", process.parent_pid);
    printf("    %5d |", process.priority);
    printf(" %11d |", process.num_kids);
    switch (process.status) {
        case -1:
            printf("  %s   |", "EMPTY");
            break;
        case 1:
            printf("  %s |", "RUNNING");
            break;
        case 2:
            printf("  %s   |", "READY");
            break;
        case 3:
            printf("  %s |", "BLOCKED");
            break;
        case 4:
            printf("   %s   |", "ZAPPED");
            break;
        case 5:
            printf("   %s   |", "QUIT");
            break;
        default:
            printf("    %d    |", process.status);
            break;
    }
    printf(" %8d |", process.total_run_time);
    printf(" %-50s |\n", process.name);
}


/**
 *          Name -> dump_processes
 *       Purpose -> print the entire process table with required metadata
 *    Parameters -> none
 *       Returns -> none
 *  Side Effects -> none
 */
int dump_all = 1;
void dump_processes() {
    printf("----------------------------------------------------------------------------------------------------------------------\n");
    printf("|  %s  | %s | %s | %s |  %s  | %s | %-50s | \n", "PID", "Parent", "Priority", "Child Count", "Status", "CPU Time", "Name");
    printf("----------------------------------------------------------------------------------------------------------------------\n");
    for (int i = 0; i < MAXPROC; i++) {
        proc_struct p = ProcTable[i];
        if (!dump_all && p.pid >= 0) {
            dump_process(ProcTable[i]);
            continue;
        }
        dump_process(ProcTable[i]);
    }
    printf("---------------------------------------------------------------------------------------------------------------------\n");
}


/**
 *          Name -> getpid
 *       Purpose -> get the pid of the Current process
                    required for some testcases
 *    Parameters -> none
 *       Returns -> none
 *  Side Effects -> none
 */
int getpid(){
    if (Current){
        return Current->pid;
    }
    return -1;
}


/* -------------------------  Support for later phases ----------------------------------- */


/**
 *          Name -> block_me
 *       Purpose -> block the currently running process with a new status
 *    Parameters -> int new_status - status to use for the CUrrent process
 *       Returns -> success status of blocking operation, 0 for success, -1 for failure
                    halts on critical failure
 *  Side Effects -> Current process is now block and dispatcher will be called.
 */
int block_me(int new_status){
    if (new_status <= 10){
        console("block_me(): status not greater than 10\n");
        halt(1);
    }
    if (Current->status == ZAPPED){
        console("block_me(): attempting to block a zapped process\n");
        return -1;
    }

    Current->status = new_status;
    remove_from_ready_list(Current);
    dispatcher();
    return 0;
}


/**
 *          Name -> unblock_proc
 *       Purpose -> unblock a process, clean up metadata
 *                    reset status to READY.
 *    Parameters -> int new_status - status to use for the CUrrent process
 *       Returns -> success status of blocking operation, 0 for success, -1 for zap failure,
 *                    -2 for nonexistent process failure.
 *  Side Effects -> Current process is now READY and dispatcher will be called.
 */
int unblock_proc(int pid){
    if (Current->status == ZAPPED){
        console("unblock_proc(): attempting to unblock a zapped process\n");
        return -1;
    }

    for(int i = 1; i < MAXPROC; i++){
        proc_struct proc = ProcTable[i];
        if (proc.pid == pid && proc.pid != Current->pid){
            proc.status = READY;
            insert_into_ready_list(&proc);
            dispatcher();
            return 0;
        }
    }
    console("unblock_proc(): attempting to unblock PID that does not exist\n");
    return -2;
}


/**
 *          Name -> read_cur_start_time
 *       Purpose -> read the Current process start time.
                    required for later phases
 *    Parameters -> none
 *       Returns -> Current process start time or -1 if no Current process
 *  Side Effects -> none
 */
int read_cur_start_time(void){
    if (Current){
        return Current->start_time;
    }
    return -1;
}


/**
 *          Name -> time_slice
 *       Purpose -> read the Current process start time.
                    required for later phases
 *    Parameters -> none
 *       Returns -> Current process start time or -1 if no Current process
 *  Side Effects -> none
 */
void time_slice(void){
    Current->current_run_time = sys_clock() - Current->start_time;
    if (Current->current_run_time > TIME_SLICE) {
        dispatcher();
    }
}
