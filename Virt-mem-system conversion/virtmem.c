/*
 * Some starter code for CSC 360, Spring 2025, Assignment #4
 *
 * Prepared by: 
 * Michael Zastre (University of Victoria) -- 2024
 * 
 * Modified for ease-of-use and marking by 
 * Konrad Jasman (University of Victoria) -- 2025
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <unistd.h>
 
 /*
  * Some compile-time constants.
  */
 
 #define REPLACE_NONE 0
 #define REPLACE_FIFO 1
 #define REPLACE_LRU  2
 #define REPLACE_CLOCK 3
 #define REPLACE_OPTIMAL 4
 
 
 #define TRUE 1
 #define FALSE 0
 #define PROGRESS_BAR_WIDTH 60
 #define MAX_LINE_LEN 100
 
 
 /*
  * Some function prototypes to keep the compiler happy.
  */
 int setup(void);
 int teardown(void);
 int output_report(void);
 long resolve_address(long, int);
 void error_resolve_address(long, int);
 
 
 /*
  * Variables used to keep track of the number of memory-system events
  * that are simulated.
  */
 int page_faults = 0;
 int mem_refs    = 0;
 int swap_outs   = 0;
 int swap_ins    = 0;
 
 
 /*
  * Page-table information. You are permitted to modify this in order to
  * implement schemes such as CLOCK. However, you are not required
  * to do so.
  */
 struct page_table_entry *page_table = NULL;
 struct page_table_entry {
    long page_num;         // Virtual page number
    int  dirty;            // Has this page been written to?
    int  free;             // Is this frame free?
    int  reference;        // CLOCK reference/use bit
    long last_access_time; // For LRU, store the 'time' last accessed
};

 /*
  * These global variables will be set in the main() function. The default
  * values here are non-sensical, but it is safer to zero out a variable
  * rather than trust to random data that might be stored in it -- this
  * helps with debugging (i.e., eliminates a possible source of randomness
  * in misbehaving programs).
  */
 
 int size_of_frame = 0;  /* power of 2 */
 int size_of_memory = 0; /* number of frames */
 int page_replacement_scheme = REPLACE_NONE;



long global_time = 0; // For each memory reference, increment this counter.

int fifo_ptr = 0;    // used to pick next victim for FIFO

int clock_hand = 0; // a "clock hand" index that circles through frames.

/*
 * function to get a victim frame based on the chosen scheme
 */
int get_victim_frame() {
    int victim = -1;

    if (page_replacement_scheme == REPLACE_FIFO) {
        /*
         * For FIFO, just pick the frame at fifo_ptr,
         * then increment fifo_ptr (mod size_of_memory).
         */
        victim = fifo_ptr;
        fifo_ptr = (fifo_ptr + 1) % size_of_memory;
        return victim;
    }
    else if (page_replacement_scheme == REPLACE_LRU) {
        /*
         * Pick the frame whose last_access_time is smallest (least recently used).
         */
        long min_time = page_table[0].last_access_time;
        victim = 0;
        int i;
        for (i = 1; i < size_of_memory; i++) {
            if (page_table[i].last_access_time < min_time) {
                min_time = page_table[i].last_access_time;
                victim = i;
            }
        }
        return victim;
    }
    else if (page_replacement_scheme == REPLACE_CLOCK) {
        /*
         * The standard CLOCK algorithm:
         * Move the clock_hand until you find a frame with reference=0.
         * If reference=1, set it to 0 and keep going.
         */
        while (TRUE) {
            if (page_table[clock_hand].reference == 0) {
                victim = clock_hand;
                clock_hand = (clock_hand + 1) % size_of_memory;
                return victim;
            } else {
                // give it a second chance
                page_table[clock_hand].reference = 0;
                clock_hand = (clock_hand + 1) % size_of_memory;
            }
        }
    }
    else {
        /*
         * If somehow we reach here with no valid scheme,
         * just evict frame 0 (this shouldn't happen if main logic is correct).
         */
        return 0;
    }
}

/*
 * function to handle page eviction:
 *   - if victim is dirty, increment swap_out
 *   - load new page (swap_in++)
 *   - set victim's info accordingly
 */
int evict_and_replace(int victim_frame, long new_page, int is_write) {
    // if victim page was dirty, increment swap_out
    if (page_table[victim_frame].dirty == TRUE) {
        swap_outs++;
    }

    // load new page => swap_in
    swap_ins++;

    // overwrite victim frame
    page_table[victim_frame].page_num = new_page;
    page_table[victim_frame].dirty    = is_write ? TRUE : FALSE;
    page_table[victim_frame].free     = FALSE;

    // reset reference bit for CLOCK
    page_table[victim_frame].reference = 1;
    // reset the last_access_time
    page_table[victim_frame].last_access_time = global_time;

    return victim_frame;
}

/*
  * Function to convert a logical address into its corresponding 
  * physical address. The value returned by this function is the
  * physical address (or -1 if no physical address can exist for
  * the logical address given the current page-allocation state.
  */

long resolve_address(long logical, int memwrite) {
    int i;
    long page, frame;
    long offset;
    long mask = 0;
    long effective;

    global_time++;  // each reference increments "time" for LRU

    /* Extract page number and offset. 2^size_of_frame = #bits for offset. */
    page = (logical >> size_of_frame);

    mask = 0;
    for (i = 0; i < size_of_frame; i++) {
        mask = (mask << 1) | 1;
    }
    offset = logical & mask;

    /* Find if page is already loaded in some frame. */
    frame = -1;
    for (i = 0; i < size_of_memory; i++) {
        if (!page_table[i].free && page_table[i].page_num == page) {
            frame = i;
            break;
        }
    }

    /* If found, update info (LRU time, reference bit, dirty if write). */
    if (frame != -1) {
        // Access existing page
        if (memwrite) {
            page_table[frame].dirty = TRUE;
        }
        // For CLOCK
        page_table[frame].reference = 1;
        // For LRU
        page_table[frame].last_access_time = global_time;

        effective = (frame << size_of_frame) | offset;
        return effective;
    }

    /* Page fault! Need to load the page in memory. */
    page_faults++;

    /* Look for a free frame first. */
    frame = -1;
    for (i = 0; i < size_of_memory; i++) {
        if (page_table[i].free) {
            frame = i;
            break;
        }
    }

    if (frame != -1) {
        /* Found a free frame => use it. */
        page_table[frame].page_num = page;
        page_table[frame].free     = FALSE;
        page_table[frame].dirty    = memwrite ? TRUE : FALSE;
        page_table[frame].reference = 1;  // for CLOCK
        page_table[frame].last_access_time = global_time; // for LRU

        swap_ins++;

        effective = (frame << size_of_frame) | offset;
        return effective;
    } else {
        /*
         * If no free frame, we must pick a victim (FIFO, LRU, CLOCK),
         *     evict it, then load new page into that frame.
         */
        if (page_replacement_scheme == REPLACE_NONE) {
            // No replacement scheme => we fail
            return -1;
        }

        int victim_frame = get_victim_frame();
        evict_and_replace(victim_frame, page, memwrite);

        effective = (victim_frame << size_of_frame) | offset;
        return effective;
    }
}

 /*
  * Super-simple progress bar.
  */
void display_progress(int percent){
    int to_date = PROGRESS_BAR_WIDTH * percent / 100;
    static int last_to_date = 0;
    int i;

    if (last_to_date < to_date){
        last_to_date = to_date;
    } else {
        return;
    }

    printf("Progress [");
    for (i=0; i<to_date; i++){
        printf(".");
    }
    for (; i<PROGRESS_BAR_WIDTH; i++){
        printf(" ");
    }
    printf("] %3d%%", percent);
    printf("\r");
    fflush(stdout);
}

int setup(){
    int i;

    page_table = (struct page_table_entry *)malloc(
        sizeof(struct page_table_entry) * size_of_memory
    );
    if (page_table == NULL){
        fprintf(stderr,
            "Simulator error: cannot allocate memory for page table.\n");
        exit(1);
    }

    for (i = 0; i < size_of_memory; i++){
        page_table[i].free     = TRUE;
        page_table[i].page_num = -1;
        page_table[i].dirty    = FALSE;
        page_table[i].reference = 0;
        page_table[i].last_access_time = 0;
    }

    fifo_ptr = 0;    // for FIFO
    clock_hand = 0;  // for CLOCK
    global_time = 0; // for LRU

    return 0;
}

/*
 * Teardown: free allocated structures if needed.
 */
int teardown(){
    if (page_table) {
        free(page_table);
        page_table = NULL;
    }
    return 0;
}

/*
 * If resolve_address() returns -1
 */
void error_resolve_address(long a, int l){
    fprintf(stderr, "\n");
    fprintf(stderr, 
        "Simulator error: cannot resolve address 0x%lx at line %d\n",
        a, l
    );
    exit(1);
}

int output_report(){
    printf("\n");
    printf("Memory references: %d\n", mem_refs);
    printf("Page faults: %d\n", page_faults);
    printf("Swap ins: %d\n", swap_ins);
    printf("Swap outs: %d\n", swap_outs);

    return 0;
}

int main(int argc, char **argv){
    /* For working with command-line arguments. */
    int i;
    char *s;

    /* For working with input file. */
    FILE *infile = NULL;
    char *infile_name = NULL;
    struct stat infile_stat;
    int  line_num = 0;
    int  infile_size = 0;

    /* For processing each individual line in the input file. */
    char buffer[MAX_LINE_LEN];
    long addr;
    char addr_type;
    int  is_write;

    /* For making visible the work being done by the simulator. */
    int show_progress = FALSE;

    /* Parse the command line. */
    for (i = 1; i < argc; i++){
        if (strncmp(argv[i], "--replace=", 9) == 0){
            s = strstr(argv[i], "=") + 1;
            if (strcmp(s, "fifo") == 0){
                page_replacement_scheme = REPLACE_FIFO;
            } else if (strcmp(s, "lru") == 0){
                page_replacement_scheme = REPLACE_LRU;
            } else if (strcmp(s, "clock") == 0){
                page_replacement_scheme = REPLACE_CLOCK;
            } else if (strcmp(s, "optimal") == 0){
                page_replacement_scheme = REPLACE_OPTIMAL; // not required
            } else {
                page_replacement_scheme = REPLACE_NONE;
            }
        } else if (strncmp(argv[i], "--file=", 7) == 0){
            infile_name = strstr(argv[i], "=") + 1;
        } else if (strncmp(argv[i], "--framesize=", 12) == 0){
            s = strstr(argv[i], "=") + 1;
            size_of_frame = atoi(s);
        } else if (strncmp(argv[i], "--numframes=", 12) == 0){
            s = strstr(argv[i], "=") + 1;
            size_of_memory = atoi(s);
        } else if (strcmp(argv[i], "--progress") == 0){
            show_progress = TRUE;
        }
    }

    if (infile_name == NULL){
        infile = stdin;
    } else if (stat(infile_name, &infile_stat) == 0){
        infile_size = (int)(infile_stat.st_size);
        infile = fopen(infile_name, "r");
    }

    if (page_replacement_scheme == REPLACE_NONE ||
        size_of_frame <= 0 ||
        size_of_memory <= 0 ||
        infile == NULL)
    {
        fprintf(stderr,
            "usage: %s --framesize=<m> --numframes=<n>", argv[0]);
        fprintf(stderr,
            " --replace={fifo|lru|clock} [--file=<filename>] [--progress]\n");
        exit(1);
    }

    /* Initialize data structures. */
    setup();

    /* Read the trace file line by line. */
    while (fgets(buffer, MAX_LINE_LEN-1, infile)){
        line_num++;
        if (strstr(buffer, ":")){
            sscanf(buffer, "%c: %lx", &addr_type, &addr);
            if (addr_type == 'W'){
                is_write = TRUE;
            } else {
                is_write = FALSE;
            }

            if (resolve_address(addr, is_write) == -1){
                error_resolve_address(addr, line_num);
            }
            mem_refs++;
        }

        if (show_progress && infile_size > 0) {
            display_progress(ftell(infile) * 100 / infile_size);
        }
    }

    teardown();
    output_report();

    fclose(infile);

    return 0;
}