/*
 * UVic CSC 360, Spring 2025
 * This code copyright 2025: Roshan Lasredo, Mike Zastre, David Clark, Konrad Jasman
 *
 * Assignment 3
 * --------------------
 * 	Simulate a Multi-Level Feedback Queue with `3` Levels/Queues each 
 * 	implementing a Round-Robin scheduling policy with a Time Quantum
 * 	of `2`, `4` and `8` resp, and including a boost mechanism.
 * 
 * Input: Command Line args
 * ------------------------
 * 	./feedbackq <input_test_case_file>
 * 	e.g.
 * 	     ./feedbackq test1.txt
 * 
 * Input: Test Case file
 * ---------------------
 * 	Each line corresponds to an instruction and is formatted as:
 *
 * 	<event_tick>,<task_id>,<burst_time>
 * 
 * 	NOTE: 
 * 	1) All times are represented as `whole numbers`.
 * 	2) Special Case:
 * 	     burst_time =  0 -- Task Creation
 * 	     burst_time = -1 -- Task Termination
 * 
 * 
 * Assumptions: (For Multi-Level Feedback Queue)
 * -----------------------
 * 	1) On arrival of a Task with the same priority as the current 
 * 		Task, the current Task is not preempted.
 * 	2) A Task on being preempted is added to the end of its queue.
 * 	3) Arrival tick, Burst tick and termination tick for the same  
 * 		Task will never overlap. But the arrival/exit of one  
 * 		Task may overlap with another Task.
 * 	4) Tasks will be labelled from 1 to 10.
 * 	5) The event_ticks in the test files will be in sorted order.
 * 	6) Once a Task is assigned a queue, it will always continue to 
 * 		run in that queue for any new future bursts (Unless further 
 * 		demoted, or returned to queue 1 by a boost).
 * 	7) Task termination instruction will always come after the 
 * 		Task completion for the given test case.
 * 	8) Task arrival/termination/boosting does not consume CPU cycles.
 * 	9) A task is enqueued into one of the queues only if it requires
 * 		CPU bursts.
 * 	
 * Output:
 * -----------------------
 * 	NOTE: Do not modify the formatting of the print statements.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include "queue.h"


/* 
 * Some constants related to assignment description.
 */
#define MAX_INPUT_LINE 100
#define MAX_TASKS 10
#define BOOST_INTERVAL 25

/*
 * MLFQ has three queues: Q1=2 ticks, Q2=4 ticks, Q3=8 ticks
 */
const int QUEUE_TIME_QUANTUMS[] = { 2, 4, 8 };

/* Global Queues for MLFQ */
Queue_t *queue_1;
Queue_t *queue_2;
Queue_t *queue_3;

/* Global Task Table (up to 10 tasks) */
Task_t task_table[MAX_TASKS];

/* Currently running task + the time slice left for it */
Task_t *current_task = NULL;
int remaining_quantum = 0;

/*
 * validate_args():
 *   We need exactly one command line arg: the input file name.
 */
void validate_args(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        exit(1);
    }
}

/*
 * initialize_vars():
 *   Sets up empty queues and resets the task table.
 */
void initialize_vars() {
    queue_1 = init_queue();
    queue_2 = init_queue();
    queue_3 = init_queue();

    for (int i = 0; i < MAX_TASKS; i++) {
        task_table[i].id                   = 0;  // 0 => "unused"
        task_table[i].burst_time           = 0;
        task_table[i].remaining_burst_time = 0;
        task_table[i].current_queue        = 0;
        task_table[i].total_wait_time      = 0;
        task_table[i].total_execution_time = 0;
        task_table[i].next                 = NULL;
    }

    current_task      = NULL;
    remaining_quantum = 0;
}

/*
 * Function: read_instruction
 * --------------------------
 *  Reads a single line from the input file and stores the 
 *  appropriate values in the instruction pointer provided. In case
 *  `EOF` is encountered, the `is_eof` flag is set.
 *
 *  fp: File pointer to the input test file
 *  instruction: Pointer to store the read instruction details
 */

void read_instruction(FILE *fp, Instruction_t *instruction) {
    char line[MAX_INPUT_LINE];
    if (fgets(line, sizeof(line), fp) == NULL) {
        instruction->event_tick = -1;
        instruction->is_eof     = true;
        return;
    }

    int vars_read = sscanf(line, "%d,%d,%d",
                           &instruction->event_tick,
                           &instruction->task_id,
                           &instruction->burst_time);
    if (vars_read != 3) {
        fprintf(stderr, "Malformed input line: %s\n", line);
        exit(1);
    }

    instruction->is_eof = false;
    if (instruction->event_tick < 0 || instruction->task_id < 0) {
        fprintf(stderr, "Incorrect file input.\n");
        exit(1);
    }
}

/*
 * Function: get_queue_by_id
 * -------------------------
 *  Returns the Queue associated with the given `queue_id`.
 *
 *  queue_id: Integer Queue identifier.
 */

Queue_t *get_queue_by_id(int queue_id) {
    switch (queue_id) {
        case 1: return queue_1;
        case 2: return queue_2;
        case 3: return queue_3;
    }
    return NULL; // Should not happen if queue_id is 1..3
}

/*
 * remove_task_from_queue():
 *   Remove a given Task_t pointer from a queue, if present.
 */
void remove_task_from_queue(Queue_t *q, Task_t *t) {
    if (is_empty(q)) return;

    if (q->start == t) {
        q->start = t->next;
        if (q->start == NULL) {
            q->end = NULL;
        }
        t->next = NULL;
        return;
    }

    Task_t *prev = q->start;
    while (prev->next && prev->next != t) {
        prev = prev->next;
    }
    if (prev->next == t) {
        prev->next = t->next;
        if (prev->next == NULL) {
            q->end = prev;
        }
        t->next = NULL;
    }
}

/*
 * remove_task_from_all_queues():
 *   Remove the task from Q1, Q2, Q3 if it appears.
 */
void remove_task_from_all_queues(Task_t *t) {
    remove_task_from_queue(queue_1, t);
    remove_task_from_queue(queue_2, t);
    remove_task_from_queue(queue_3, t);
}

/*
 * 	 preempt_if_higher_priority_arrived():
 *   If the currently running task is in a lower queue (queue_2 or queue_3)
 *   and a new task arrives in a strictly higher queue (queue_1 < current_task->queue),
 *   we must preempt. The assignment states we do *not* preempt if the new arrival
 *   has the *same* priority, but we *do* if it's strictly higher.
 *
 *   Called each time a new task arrives with burst_time>0, *before* we enqueue it,
 *   so that the new higher-priority task can run immediately at this tick.
 */
void preempt_if_higher_priority_arrived(Task_t *new_task) {
    // If no current_task, no preemption needed
    if (current_task == NULL) return;

    // If new arrival is higher priority (smaller queue ID) than current
    if (new_task->current_queue < current_task->current_queue) {
        // Preempt current_task:
        // 1) Put current_task at back of its queue
        enqueue(get_queue_by_id(current_task->current_queue), current_task);
        // 2) Clear current_task so scheduler can pick new arrival
        current_task = NULL;
        remaining_quantum = 0;
    }
}

/*
 * Function: handle_instruction
 * ----------------------------
 *  Processes the input instruction, depending on the instruction
 *  type:
 *      a. New Task (burst_time == 0)
 *      b. Task Completion (burst_time == -1)
 *      c. Task Burst (burst_time == <int>)
 *
 *  NOTE: 
 *	a. This method performs NO task scheduling, NO Preemption and NO
 *  	Updation of Task priorities/levels. These tasks would be   
 *		handled by the `scheduler`.
 *	b. A task once demoted to a level, retains that level for all 
 *		future bursts unless it is further demoted or boosted.
 *
 *  instruction: Input instruction
 *  tick: Clock tick (ONLY For Print statements)
 */
 
void handle_instruction(Instruction_t *instruction, int tick) {
    int task_id = instruction->task_id;
    Task_t *t   = &task_table[task_id];

    if (instruction->burst_time == 0) {
        // NEW task
        t->id                   = task_id;
        t->burst_time           = 0;
        t->remaining_burst_time = 0;
        t->current_queue        = 1;  // always starts in top queue
        t->total_wait_time      = 0;
        t->total_execution_time = 0;
        t->next                 = NULL;

        printf("[%05d] id=%04d NEW\n", tick, task_id);

    } else if (instruction->burst_time == -1) {
        // EXIT
        int waiting_time     = t->total_wait_time;
        int turn_around_time = t->total_wait_time + t->total_execution_time;

        printf("[%05d] id=%04d EXIT wt=%d tat=%d\n",
               tick, task_id, waiting_time, turn_around_time);

        // Remove from queues
        remove_task_from_all_queues(t);

        // If this was the current running task, relinquish CPU
        if (current_task && current_task->id == task_id) {
            current_task = NULL;
            remaining_quantum = 0;
        }

        // Zero out the structure
        t->id = 0;
        t->burst_time = 0;
        t->remaining_burst_time = 0;
        t->current_queue = 0;
        t->total_wait_time = 0;
        t->total_execution_time = 0;
        t->next = NULL;

    } else {
        // A CPU burst requirement: new_task needs CPU time
        t->burst_time           = instruction->burst_time;
        t->remaining_burst_time = instruction->burst_time;

        /*
         * (NEW) Preempt if the new task is strictly higher priority
         * than the currently running task. The assignment states
         * no preemption if same priority, but does imply preemption
         * for higher priority.
         */
        preempt_if_higher_priority_arrived(t);

        // Now queue the new CPU burst
        enqueue(get_queue_by_id(t->current_queue), t);
    }
}

/*
 * Function: peek_priority_task
 * ----------------------------
 *  Returns a reference to the Task with the highest priority.
 *  Does NOT dequeue the task.
 */

Task_t *peek_priority_task() {
    if (!is_empty(queue_1)) return queue_1->start;
    if (!is_empty(queue_2)) return queue_2->start;
    if (!is_empty(queue_3)) return queue_3->start;
    return NULL;
}

/*
 * Function: decrease_task_level
 * -----------------------------
 *  Updates the task to lower its level(Queue) by 1.
 */
void decrease_task_level(Task_t *task) {
	task->current_queue = task->current_queue == 3 ? 3 : task->current_queue + 1;
}

/*
 * Function: boost
 * -----------------------------
 *  If the current tick is a multiple of the BOOST_INTERVAL, perform a boost
 *  on all tasks in Queue 3, followed by a boost on all tasks in Queue
 *  2.  A boost is done by dequeuing the task from its current queue 
 *  and queuing it into Queue 1.  At the end of this process, all tasks
 *  with remaining CPU bursts should be in Queue 1.  The current task
 *  should be unaffected, except that its remaining quantum should be
 *  set to a maximum of 2 (or left unchanged if it's less than two). 
 *  Boosts do not take CPU time.
 */


void boost(int tick) {
    if (tick % BOOST_INTERVAL != 0) {
        return;
    }

    // Move tasks from queue_3 -> queue_1
    while (!is_empty(queue_3)) {
        Task_t *tmp = dequeue(queue_3);
        tmp->current_queue = 1;
        enqueue(queue_1, tmp);
    }
    // Move tasks from queue_2 -> queue_1
    while (!is_empty(queue_2)) {
        Task_t *tmp = dequeue(queue_2);
        tmp->current_queue = 1;
        enqueue(queue_1, tmp);
    }

    // If current_task is running on Q2 or Q3 with more than 2 left, clamp it
    if (current_task && current_task->current_queue > 1) {
        current_task->current_queue = 1;
        if (remaining_quantum > 2) {
            remaining_quantum = 2;
        }
    }

    printf("[%05d] BOOST\n", tick);
}

/*
 * Function: scheduler
 * -------------------
 *  Schedules the task having the highest priority to be the current 
 *  task. Also, for the currently executing task, decreases the task    
 *	level on completion of the current time quantum.
 *
 *  NOTE:
 *  a. The task to be currently executed is `dequeued` from one of the
 *  	queues.
 *  b. On Pre-emption of a task by another task, the preempted task 
 *  	is `enqueued` to the end of its associated queue.
 */

void scheduler() {
    if (current_task != NULL && remaining_quantum > 0) {
        // still have time quantum left, so keep running
        return;
    }

    // We need a new current_task
    current_task = NULL;
    remaining_quantum = 0;

    for (int q_id = 1; q_id <= 3; q_id++) {
        Queue_t *q = get_queue_by_id(q_id);
        while (!is_empty(q)) {
            Task_t *front = q->start;
            // If front is invalid, discard it
            if (front->id == 0 || front->remaining_burst_time <= 0) {
                dequeue(q);
                continue;
            }
            // Found a valid candidate
            current_task = dequeue(q);
            remaining_quantum = QUEUE_TIME_QUANTUMS[current_task->current_queue - 1];
            return;
        }
    }
    // If we get here, all queues empty => current_task stays NULL => IDLE
}


/*
 * Function: execute_task
 * ----------------------
 *  Executes the current task (By updating the associated remaining
 *  times). Sets the current_task to NULL on completion of the
 *	current burst.
 *
 *  tick: Clock tick (ONLY For Print statements)
 */

void execute_task(int tick) {
    if (current_task) {
        // 1) Use one CPU tick
        current_task->remaining_burst_time--;
        remaining_quantum--;

        // 2) This tick => execution time
        current_task->total_execution_time++;

        // 3) Print
        int used_so_far = current_task->burst_time - current_task->remaining_burst_time;
        printf("[%05d] id=%04d req=%d used=%d queue=%d\n",
               tick,
               current_task->id,
               current_task->burst_time,
               used_so_far,
               current_task->current_queue);

        // 4) Check done or quantum expiry
        if (current_task->remaining_burst_time <= 0) {
            // finished
            current_task = NULL;
        } else if (remaining_quantum == 0) {
            // demote + requeue
            decrease_task_level(current_task);
            enqueue(get_queue_by_id(current_task->current_queue), current_task);
            current_task = NULL;
        }
    } else {
        // CPU idle
        printf("[%05d] IDLE\n", tick);
    }
}

/*
 * Function: update_task_metrics
 * -----------------------------
 * 	Increments the waiting time/execution time for the tasks 
 * 	that are currently scheduled (In the queue). These values would  
 * 	be later used for the computation of the task waiting time and  
 *	turnaround time.
 */

void update_task_metrics() {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_table[i].id == 0) continue;  // unused entry
        if (task_table[i].remaining_burst_time > 0) {
            // If not current running task => waiting
            if (!current_task || (current_task->id != task_table[i].id)) {
                task_table[i].total_wait_time++;
            }
        }
    }
}

/*
 * main():
 *   The simulation loop:
 *     - read instructions for this tick
 *     - possibly boost
 *     - scheduler picks a task if none or quantum used up
 *     - update wait times
 *     - execute current task
 *     - stop if instructions finished, all queues empty, no current task
 */
int main(int argc, char *argv[]) {
    validate_args(argc, argv);
    initialize_vars();

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "File \"%s\" does not exist.\n", argv[1]);
        exit(1);
    }

    Instruction_t *curr_instruction = (Instruction_t*) malloc(sizeof(Instruction_t));
    read_instruction(fp, curr_instruction);
    if (curr_instruction->is_eof) {
        fprintf(stderr, "Error: The input file is empty.\n");
        exit(1);
    }

    int tick = 1;
    int is_inst_complete = 0;  // false

    while (1) {
        // Handle all instructions that match this tick
        while (curr_instruction->event_tick == tick) {
            handle_instruction(curr_instruction, tick);

            // read next line
            read_instruction(fp, curr_instruction);
            if (curr_instruction->is_eof) {
                is_inst_complete = 1; // true
                break;
            }
        }

        // Possibly boost
        boost(tick);

        // Let scheduler pick a task if needed
        scheduler();

        // Update wait times for tasks not running
        update_task_metrics();

        // Run 1 CPU tick
        execute_task(tick);

        // Stop if instructions complete, all queues empty, no current task
        if (is_inst_complete && is_empty(queue_1) && is_empty(queue_2)
            && is_empty(queue_3) && (current_task == NULL)) {
            break;
        }

        tick++;
    }

    fclose(fp);
    deallocate(curr_instruction);
    deallocate(queue_1);
    deallocate(queue_2);
    deallocate(queue_3);

    return 0;
}
