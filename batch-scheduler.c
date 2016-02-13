#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include "lib/random.h" 

#define BUS_CAPACITY 3
#define SENDER 0
#define RECEIVER 1
#define NORMAL 0
#define HIGH 1

/*
 *	initialize task with direction and priority
 *	call o
 * */
typedef struct {
	int direction;
	int priority;
} task_t;


struct semaphore busCapacity;        /* Used to control that only a MAX number of task use the bus at same time. */
struct semaphore lowPrioritySend;    /* Makes low priority tasks with send wait if there is high priority send still to be done. */
struct semaphore lowPriorityReceive; /* Makes low priority tasks with receive wait if there is high priority receive still to be done. */
struct semaphore controlHighPrio;    /* Controls the value change to avoid Race Condition. */
struct semaphore controlDirection;   /* Controls the direction semaphore up so only one makes it up, to avoid more than one up in parallel*/
struct semaphore controlWaiting;     /* Control the value change to avoid Race Condition. */
struct semaphore direction;          /* Controls that only one goes first to the bridge and put his direction and avoids the ones with the other direction to execute their task. */

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive);

void senderTask(void *);           /* Generates a sending task thread. */
void receiverTask(void *);         /* Generates a receiving task thread. */
void senderPriorityTask(void *);   /* Generates a high priority sending task thread. */
void receiverPriorityTask(void *); /* Generates a high priority receiving task thread. */

void oneTask(task_t task);      /* Task requires to use the bus and executes methods below. */
void getSlot(task_t task);      /* Task tries to use slot on the bus. */
void transferData(task_t task); /* Task processes data on the bus either sending or receiving based on the direction. */
void leaveSlot(task_t task);    /* Task release the slot. */
void setDirection(int);         /* Changes the direction of the bus. */
int isAnotherHighPrio(int);     /* Checks if there is another high priority task to be executed in the given direction. */
void waitingBandwidth(int);     /* Increases or decreases the number of threads waiting for bandwith, 1 increases 0 decreases. */

static unsigned int numHighPrioSend;      /* Number of remaining high priority send tasks. */
static unsigned int numHighPrioReceive;   /* Number of remaining high priority receive tasks. */
static unsigned int numLowPrioSend;       /* Number of low priority send tasks. */
static unsigned int numLowPrioReceive;    /* Number of low priority receive tasks. */
static int busDirection;                  /* Direction of the bus. */
static unsigned int numTasksWaiting;      /* Number of tasks waiting for bandwidth. */
int isSomeoneWaiting();                   /* Checks whether there is a task waiting for bandwidth or not. */

/* Initializes semaphores */ 
void init_bus(void){ 
 
    random_init((unsigned int)123456789); 
    /*Initialize the bus semaphore (which will be a barrier), to the maximum tasks allowed by the bus bandwidth*/
    sema_init(&busCapacity,BUS_CAPACITY);
	
    /*Initialize the low priority receive task semaphores, which will block/allow low priority task from being executed */
    sema_init(&lowPriorityReceive,0);

    /*Initialize the low priority send task semaphores, which will block/allow low priority task from being executed */
    sema_init(&lowPrioritySend,0);
	
    /*Initialize the control semaphores, which will control the access to the critical values, to avoid Race Condition */
    sema_init(&controlHighPrio,1);
    sema_init(&controlWaiting,1);
    sema_init(&controlDirection,1);
	
    /*Initialize the direction semaphore, which will prevent tasks from being executed if the 'direction' of the bus is not same as that certain task's and only allow one to continue and change the direction */
    sema_init(&direction,1);
	
    /*Initialize the critical variables*/
    busDirection = -1;
    numHighPrioReceive = 0;
    numHighPrioSend = 0;
    numTasksWaiting = 0;
}

/*
 *  Creates a memory bus sub-system  with num_tasks_send + num_priority_send
 *  sending data to the accelerator and num_task_receive + num_priority_receive tasks
 *  reading data/results from the accelerator.
 *
 *  Every task is represented by its own thread. 
 *  Task requires and gets slot on bus system (1)
 *  process data and the bus (2)
 *  Leave the bus (3).
 */

void batchScheduler(unsigned int num_tasks_send, unsigned int num_tasks_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive)
{	
	
    unsigned int i;
    /*Create threads for high priority send tasks */
    for (i = 0; i < num_priority_send; i++)
      thread_create("priorityHighSend", PRI_MAX, &senderPriorityTask, NULL);

    /*Create threads for high priority receive tasks */
    for (i = 0; i < num_priority_receive; i++)
      thread_create("priorityHighReceive", PRI_MAX, &receiverPriorityTask, NULL);

    /*Create threads for low priority send tasks */
    for (i = 0; i < num_tasks_send; i++)
      thread_create("priorityLowSend", PRI_MIN, &senderTask, NULL);

    /*Create threads for low priority receive tasks */
    for (i = 0; i < num_tasks_receive; i++)
      thread_create("priorityLowReceive", PRI_MIN, &receiverTask, NULL);    
}

/*Low priority task sending data to the accelerator 
 *input: unused variable (pintos requirement)
 *output: void
 */
void senderTask(void *aux UNUSED){
	task_t task = {SENDER, NORMAL};
	oneTask(task);
}

/*High priority task sending data to the accelerator 
 *input: unused variable (pintos requirement)
 *output: void
 */
void senderPriorityTask(void *aux UNUSED){
	task_t task = {SENDER, HIGH};
	oneTask(task);
}

/*Low priority task reading data from the accelerator 
 *input: unused variable (pintos requirement)
 *output: void
 */
void receiverTask(void *aux UNUSED){
	task_t task = {RECEIVER, NORMAL};
	oneTask(task);
}

/*High priority task reading data from the accelerator 
 *input: unused variable (pintos requirement)
 *output: void
 */
void receiverPriorityTask(void *aux UNUSED){
    task_t task = {RECEIVER, HIGH};
    oneTask(task);
}

/*Abstract task execution 
 *input: task_t containing the thread information
 *output: void
 */
void oneTask(task_t task) {
	getSlot(task);
	transferData(task);
	leaveSlot(task);
}


/*Task tries to get slot on the bus subsystem  
 *input: task_t containing the thread information
 *output: void
 */
void getSlot(task_t task) 
{
	if(task.priority==HIGH)
	{
		if(task.direction==SENDER)
		{
			sema_down(&controlHighPrio);
			numHighPrioSend++;
			sema_up(&controlHighPrio);
		}
		else
		{
			sema_down(&controlHighPrio);
			numHighPrioReceive++;
			sema_up(&controlHighPrio);
		}
	}
	/*In case it is a NORMAL PRIORITY TASK and there is at least one High Priority left, make it wait*/
	sema_down(&controlHighPrio);
    if(task.priority == NORMAL && (isAnotherHighPrio(SENDER) || isAnotherHighPrio(RECEIVER)))
    {
		if(task.direction==SENDER)
		{
			sema_up(&controlHighPrio);
			sema_down(&lowPrioritySend);
			sema_up(&lowPrioritySend);
		}
		else
		{
			sema_up(&controlHighPrio);
			sema_down(&lowPriorityReceive);
			sema_up(&lowPriorityReceive);
		}
    }
	sema_up(&controlHighPrio);
	/*If it is not its direction, make it wait until it can change*/
	if (busDirection != task.direction)
    {
	    sema_down(&direction);
        setDirection(task.direction);
    }
	/*Add one counter to the number of tasks waiting for bandwidth in the same direction*/
    waitingBandwidth(1);
	/*Get bandwidth*/
    sema_down(&busCapacity);
}

/*Simulates the time needed for a thread to send/receive data to/from the bus  
 *input: task_t containing the thread information
 *output: void
 */
void transferData(task_t task) 
{
	timer_sleep(random_ulong() % 5);
}

/*Task tries to leave slot on the bus subsystem 
 *input: task_t containing the thread information
 *output: void
 */
void leaveSlot(task_t task) 
{
	/*Remove one counter to the number of tasks waiting for bandwidth in the same direction*/
	waitingBandwidth(0);
	sema_up(&busCapacity);
	if (task.priority == HIGH && task.direction == SENDER) 
	{
		/* Update the number of remaining high priority send tasks (use controlHighPrio to avoid Race Condition)*/
		sema_down(&controlHighPrio);
		numHighPrioSend--;
		sema_up(&controlHighPrio);
	}

	if (task.priority == HIGH && task.direction == RECEIVER)
	{
		/* Update the number of remaining high priority receive tasks (use controlHighPrio to avoid Race Condition)*/
		sema_down(&controlHighPrio);
		numHighPrioReceive--;
		sema_up(&controlHighPrio);
	}
	if(!isAnotherHighPrio(SENDER) && !isAnotherHighPrio(RECEIVER) && task.priority == HIGH)
	{
		/*Allow NORMAL PRIORITY TASKS to be executed*/
		sema_up(&lowPrioritySend);
		sema_up(&lowPriorityReceive);
	}
	/* If the bus is empty, release 'direction' semaphore to allow one task continue even (even one with the other direction)*/
	if (isSomeoneWaiting())
	{
		sema_up(&direction);
	}
}

/*Set the direction of the bus 
 *input: int containing the direction to which the the bus has to be changed
 *output: void
 */
void setDirection(int dir)
{
	/*Lock (down) 'controlDirection' semaphore to avoid Race Condition on the critical variable*/
	sema_down(&controlDirection);
	busDirection = dir;
	sema_up(&controlDirection);
}
/*Checks if there is another high priority task to be executed in the given direction 
 *input: int containing the direction that want to be checked
 *output: int containing 1 if true; 0 if false
 */
int isAnotherHighPrio(int dir)
{
	int cont =0;
	if (dir == SENDER)
	{
        /*Control the access to the critical variable*/
        cont=numHighPrioSend;
		if(cont==0) /*If there is no other high priority send task return 0 (false for if condition)*/
		{
			return 0;
		}
		else /*If there is other high priority send task return 1 (true for if condition)*/
		{
			return 1;  
		}
	}
	else
	{
		/*Control the access to the critical variable*/
        cont=numHighPrioReceive;
		if(cont==0) /*If there is no other high priority receive task return 0 (false for if condition)*/
		{
			return 0;
		}
		else /*If there is other high priority receive task return 1 (true for if condition)*/
		{
			return 1;  
		}
	}
}

/*Increases or decreases the number of threads waiting for bandwith, 1 increases 0 decreases 
 *input: int containing the value which determines whether the bandwidth has to be increased or decreased
 *output: void
 */
void waitingBandwidth(int value)
{
	if(value)
	{
		sema_down(&controlWaiting);
		numTasksWaiting++;
		sema_up(&controlWaiting);
	}
	else
	{
		sema_down(&controlWaiting);
		numTasksWaiting--;
		sema_up(&controlWaiting);
	}
}
/*Checks whether there is a task waiting for bandwidth or not
 *input: void
 *output: int containing 1 if true; 0 if false
 */
int isSomeoneWaiting()
{
	int value=0;
	sema_down(&controlWaiting);
	value=numTasksWaiting;
	sema_up(&controlWaiting);
	if(value==0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}
