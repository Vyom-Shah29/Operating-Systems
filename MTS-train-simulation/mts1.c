#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_TRAINS 75


typedef enum {
    LOW_PRIORITY,
    HIGH_PRIORITY
} Priority;

typedef enum {
    EAST,
    WEST
} Direction;

/*
 *   - id:        train number (0-based)
 *   - direction: E, e or W, w
 *   - priority:  low priority or high priority
 *   - loadingTime, crossingTime in tenths of second
 *   - readyTime: when the train finished loading
 *   - canCrossCond: condition var for scheduler
 *   - isCrossing: set to 1 by the scheduler when its train's turn
 *   - doneCrossing: set to 1 by the train when it has finished crossing
 */
typedef struct {
    int             id;
    Direction       direction;
    Priority        priority;
    int             loadingTime;   // in tenths of a second
    int             crossingTime;  // in tenths of a second
    double          readyTime;     // when loading finished
    pthread_cond_t  canCrossCond;
    int             isCrossing;    // set by scheduler
    int             doneCrossing;  // set by the train once off track
} TrainInfo;


//All trains input
static TrainInfo g_trains[MAX_TRAINS];
static int       g_trainCount = 0;

//ready list of indices into g_trains[] for trains that have finished loading but not yet crossed
static int       g_readyTrains[MAX_TRAINS];
static int       g_readyCount = 0;

//finished crossing. Once it equals g_trainCount
static int       g_trainsDone = 0;


static int       g_anyTrainCrossed = 0;
static Direction g_lastDirectionUsed;
static int       g_consecutiveDirectionCount = 0;


//start time so we can for computing elapsed time
static struct timeval simulationStartTime;

//how many real seconds have elapsed
double getSimulationTime()
{
    struct timeval current;
    gettimeofday(&current, NULL);

    double startSec = (double)simulationStartTime.tv_sec +
                      (double)simulationStartTime.tv_usec / 1000000.0;
    double nowSec   = (double)current.tv_sec +
                      (double)current.tv_usec / 1000000.0;

    return nowSec - startSec;
}


void formatSimTime(char* buffer, size_t bufSize, double simTime)
{
    int hours   = (int)(simTime / 3600);
    int minutes = (int)((int)simTime % 3600 / 60);
    double secs = simTime - (hours * 3600) - (minutes * 60);

    snprintf(buffer, bufSize, "%02d:%02d:%04.1f", hours, minutes, secs);
}


// Synchornization

static pthread_mutex_t g_schedulerMutex = PTHREAD_MUTEX_INITIALIZER;


static pthread_cond_t  g_trainReadyCond = PTHREAD_COND_INITIALIZER;

// -------------------- pickNextTrain() --------------------

int pickNextTrain()
{
    //Check if there's at least one high-priority train
    int haveHigh = 0;
    for (int i = 0; i < g_readyCount; i++) {
        TrainInfo* t = &g_trains[g_readyTrains[i]];
        if (t->priority == HIGH_PRIORITY) {
            haveHigh = 1;
            break;
        }
    }
    Priority pickPriority = haveHigh ? HIGH_PRIORITY : LOW_PRIORITY;

    //candidate list of trains with priority = pickPriority
    int candidate[MAX_TRAINS];
    int ccount = 0;
    for (int i = 0; i < g_readyCount; i++) {
        TrainInfo* t = &g_trains[g_readyTrains[i]];
        if (t->priority == pickPriority) {
            candidate[ccount++] = g_readyTrains[i];
        }
    }
    if (ccount == 0) {
        return -1;
    }

    //Starvation if 2 consecutive trains in the same direction,
    //see if there's a candidate in the opposite direction.
 
    if (g_consecutiveDirectionCount >= 2) {
        Direction opp = (g_lastDirectionUsed == EAST) ? WEST : EAST;
        int foundOpposite = 0;
        for (int i = 0; i < ccount; i++) {
            TrainInfo* t = &g_trains[candidate[i]];
            if (t->direction == opp) {
                foundOpposite = 1;
                break;
            }
        }
        if (foundOpposite) {
            //NOT in the opposite direction
            int tmpList[MAX_TRAINS];
            int tmpCount = 0;
            for (int i = 0; i < ccount; i++) {
                TrainInfo* t = &g_trains[candidate[i]];
                if (t->direction == opp) {
                    tmpList[tmpCount++] = candidate[i];
                }
            }
            memcpy(candidate, tmpList, tmpCount * sizeof(int));
            ccount = tmpCount;
        }
    }

     
    //If multiple remain with the same priority, but different directions,
    //pick the direction opposite the last used (or West).
     
    if (ccount > 1) {
        int eastCount = 0, westCount = 0;
        for (int i = 0; i < ccount; i++) {
            if (g_trains[candidate[i]].direction == EAST) eastCount++;
            else westCount++;
        }
        if (eastCount > 0 && westCount > 0) {
            Direction desiredDir;
            if (!g_anyTrainCrossed) {
                desiredDir = WEST; // If no train crossed, West
            } else {
                desiredDir = (g_lastDirectionUsed == EAST) ? WEST : EAST;
            }
            
            int tmpList[MAX_TRAINS];
            int tmpCount = 0;
            for (int i = 0; i < ccount; i++) {
                if (g_trains[candidate[i]].direction == desiredDir) {
                    tmpList[tmpCount++] = candidate[i];
                }
            }
            if (tmpCount > 0) {
                memcpy(candidate, tmpList, tmpCount * sizeof(int));
                ccount = tmpCount;
            }
        }
    }

   
    //If multiple trains remain (same direction), pick earliest readyTime 
    //(tie for lower ID).
  
    if (ccount > 1) {
        for (int i = 0; i < ccount - 1; i++) {
            for (int j = i + 1; j < ccount; j++) {
                TrainInfo* A = &g_trains[candidate[i]];
                TrainInfo* B = &g_trains[candidate[j]];
                if (A->readyTime > B->readyTime) {
                    int tmp = candidate[i];
                    candidate[i] = candidate[j];
                    candidate[j] = tmp;
                } else if (A->readyTime == B->readyTime) {
                    // tie => compare ID
                    if (A->id < B->id) {
                        int tmp = candidate[i];
                        candidate[i] = candidate[j];
                        candidate[j] = tmp;
                    }
                }
            }
        }
    }

    return candidate[0];
}

// Scheduler --------------------

void runScheduler()
{
    pthread_mutex_lock(&g_schedulerMutex);

    while (g_trainsDone < g_trainCount) {
        // if no trains ready
        while (g_readyCount == 0 && g_trainsDone < g_trainCount) {
            pthread_cond_wait(&g_trainReadyCond, &g_schedulerMutex);
        }

        if (g_trainsDone >= g_trainCount) {
            break; 
        }

        int chosen = pickNextTrain();
        if (chosen < 0) {
        
            continue;
        }

        // Remove from g_readyTrains 
        int removePos = -1;
        for (int i = 0; i < g_readyCount; i++) {
            if (g_readyTrains[i] == chosen) {
                removePos = i;
                break;
            }
        }
        if (removePos >= 0) {
            for (int i = removePos; i < g_readyCount - 1; i++) {
                g_readyTrains[i] = g_readyTrains[i + 1];
            }
            g_readyCount--;
        }

        // isCrossing = 1, update last direction usag
        TrainInfo* t = &g_trains[chosen];
        t->isCrossing = 1;

        if (!g_anyTrainCrossed) {
            g_lastDirectionUsed = t->direction;
            g_consecutiveDirectionCount = 1;
            g_anyTrainCrossed = 1;
        } else {
            if (t->direction == g_lastDirectionUsed) {
                g_consecutiveDirectionCount++;
            } else {
                g_lastDirectionUsed = t->direction;
                g_consecutiveDirectionCount = 1;
            }
        }

        // Signaling the train so it can proceed to cross
        pthread_cond_signal(&t->canCrossCond);

        while (!t->doneCrossing && g_trainsDone < g_trainCount) {
            pthread_cond_wait(&g_trainReadyCond, &g_schedulerMutex);
        }
    }

    pthread_mutex_unlock(&g_schedulerMutex);
}

//Train Thread --------------------
void* trainThread(void* arg)
{
    TrainInfo* train = (TrainInfo*)arg;

    usleep(train->loadingTime * 100000); // to microseconds

    // readyTime
    double simTime = getSimulationTime();
    train->readyTime = simTime;

    {
        char buf[16];
        formatSimTime(buf, sizeof(buf), simTime);
        printf("%s Train %2d is ready to go %4s\n",
               buf,
               train->id,
               (train->direction == EAST) ? "East" : "West");
        fflush(stdout);
    }

    // Add to ready list
    pthread_mutex_lock(&g_schedulerMutex);
    g_readyTrains[g_readyCount++] = train->id;
    pthread_cond_broadcast(&g_trainReadyCond);

    // Wait until the scheduler picks train
    while (!train->isCrossing) {
        pthread_cond_wait(&train->canCrossCond, &g_schedulerMutex);
    }
    pthread_mutex_unlock(&g_schedulerMutex);

    // ON main track
    simTime = getSimulationTime();
    {
        char buf[16];
        formatSimTime(buf, sizeof(buf), simTime);
        printf("%s Train %2d is ON the main track going %4s\n",
               buf,
               train->id,
               (train->direction == EAST) ? "East" : "West");
        fflush(stdout);
    }


    usleep(train->crossingTime * 100000);

    // OFF main track
    simTime = getSimulationTime();
    {
        char buf[16];
        formatSimTime(buf, sizeof(buf), simTime);
        printf("%s Train %2d is OFF the main track after going %4s\n",
               buf,
               train->id,
               (train->direction == EAST) ? "East" : "West");
        fflush(stdout);
    }

    pthread_mutex_lock(&g_schedulerMutex);
    train->doneCrossing = 1;
    g_trainsDone++;
    pthread_cond_broadcast(&g_trainReadyCond);
    pthread_mutex_unlock(&g_schedulerMutex);

    return NULL;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    gettimeofday(&simulationStartTime, NULL);

    
    FILE* fp = fopen(argv[1], "r");
    if (!fp) {
        perror("Failed to open file");
        return 1;
    }

    while (!feof(fp)) {
        char directionChar;
        int loadT, crossT;
        
        int itemsRead = fscanf(fp, " %c %d %d", &directionChar, &loadT, &crossT);
        if (itemsRead == 3) {
            TrainInfo* t = &g_trains[g_trainCount];
            t->id             = g_trainCount;
            t->readyTime      = 0.0;
            t->isCrossing     = 0;
            t->doneCrossing   = 0;
            pthread_cond_init(&t->canCrossCond, NULL);

            switch (directionChar) {
                case 'e':
                    t->direction = EAST;
                    t->priority  = LOW_PRIORITY;
                    break;
                case 'E':
                    t->direction = EAST;
                    t->priority  = HIGH_PRIORITY;
                    break;
                case 'w':
                    t->direction = WEST;
                    t->priority  = LOW_PRIORITY;
                    break;
                case 'W':
                    t->direction = WEST;
                    t->priority  = HIGH_PRIORITY;
                    break;
                default:
                    fprintf(stderr, "Unknown direction character: %c\n", directionChar);
                    fclose(fp);
                    return 1;
            }
            t->loadingTime  = loadT;
            t->crossingTime = crossT;

            g_trainCount++;
            if (g_trainCount >= MAX_TRAINS) {
                fprintf(stderr, "Reached max train limit.\n");
                break;
            }
        } 
        else if (itemsRead == EOF) {
            break; 
        } 
        else {
            
            fprintf(stderr, "Warning: skipping malformed line.\n");
            while (!feof(fp) && fgetc(fp) != '\n') { /* skip */ }
        }
    }
    fclose(fp);

    // Create threads
    pthread_t threads[MAX_TRAINS];
    for (int i = 0; i < g_trainCount; i++) {
        pthread_create(&threads[i], NULL, trainThread, &g_trains[i]);
    }

    runScheduler();

    for (int i = 0; i < g_trainCount; i++) {
        pthread_join(threads[i], NULL);
    }

    // Cleanup
    for (int i = 0; i < g_trainCount; i++) {
        pthread_cond_destroy(&g_trains[i].canCrossCond);
    }
    pthread_mutex_destroy(&g_schedulerMutex);
    pthread_cond_destroy(&g_trainReadyCond);

    return 0;
}