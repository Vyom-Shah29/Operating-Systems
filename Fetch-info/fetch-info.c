#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE_LEN 256

void read_values (const char *file_path, const char *key, char *value){
	
	FILE *file = fopen (file_path, "r");
	if (!file){
		perror ("Error opening file");
		exit(EXIT_FAILURE);
	
	}

	char line [MAX_LINE_LEN];
	while(fgets(line, sizeof(line), file)){
	
		if (strstr(line, key)){
	
			char *colon = strchr(line, ':');
			if (colon){
			
				colon++;
				while (*colon == ' ' || *colon == '\t') colon++;
				strcpy(value, colon);

				value[strcspn (value, "\n")] = '\0';
			
			}
			break;
		}
	}
	fclose(file);

}

void print_sys_info(){

	char value[MAX_LINE_LEN];
	char val1[MAX_LINE_LEN];
	char val2[MAX_LINE_LEN];
	
	read_values("/proc/cpuinfo", "model name", value);
	printf("\nModel name:   %s\n", value);

	read_values("/proc/cpuinfo", "cpu cores", value);
	printf("CPU cores:   %s\n", value);
	
	
	FILE *version_file = fopen ("/proc/version", "r");
	if (version_file){
		
		fgets(val1, sizeof(val1), version_file);
		printf("Linux version:   %s", val1);
		fclose(version_file);
			
	}

	FILE *mem_file = fopen("/proc/meminfo", "r");
	if (mem_file){
	
		while (fgets(val2, sizeof(val2), mem_file)){
		
			if (strncmp(val2, "MemTotal", 8) == 0) {
			
				printf("%s", val2);
				break;
			}
		}
		fclose(mem_file);
	
	}

	FILE *uptime_file =fopen("/proc/uptime", "r");
	if (!uptime_file){
		perror("Error opening u file");
		exit(EXIT_FAILURE);
	
	}


	double uptime_secs;
	fscanf(uptime_file, "%lf", &uptime_secs);
	fclose(uptime_file);

	int days = (int) (uptime_secs / (60 * 60 * 24));
	int hours = (int) (uptime_secs / (60 * 60)) % 24;
	int mins = (int)(uptime_secs / 60) % 60;
	int secs = (int)uptime_secs % 60;

	printf("Uptime: %d days, %d hours, %d minutes, %d seconds\n", days, hours, mins, secs);
	printf("\n");

}


void print_process_info(int pid){
	char path [MAX_LINE_LEN];
	char value[MAX_LINE_LEN];

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	
	FILE *status_file = fopen(path, "r");
	if (!status_file){
		printf("\nProcess number %d not found.\n", pid);
		return;
	}

	int pnum, pgrp, session, tty, tpgid, threads;
	unsigned long vsize, context_switches;
	char comm[MAX_LINE_LEN], state;

	fscanf (status_file, "%d %s %c %d %d %d %d %d %*d %*d %*d %*d %*d %*d %*d %*d %d %*d %*d %*d %lu", &pnum, comm, &state, &pnum, &pgrp, &session, &tty, &tpgid, &threads, &context_switches);

	fclose(status_file);
	
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	
	FILE *cmdline_file =fopen(path, "r");
	if (!cmdline_file){
		fgets(value, sizeof(value), cmdline_file);
		fclose(cmdline_file);
	
	} else {
		strcpy(value, "none");
	
	}


	printf("Process number:	%d\n", pid);
	printf("Name:	%s\n", comm);
	printf("Filename (if any):  %s\n", value[0] ? value: "none");
	printf("Threads:  %d\n", threads);
	printf("Total context switches:  %lu\n", context_switches);

}

int main (int argc, char *argv[]){

	if(argc == 1){
		print_sys_info();

	
	} else if (argc == 2){
		int pid = atoi (argv[1]);
		if (pid <= 0){
			printf("\nProcess number %d not found.\n", pid);
			exit(EXIT_FAILURE);
		
		}
		print_process_info(pid);

	} else {
		fprintf(stderr, "Usage %s [pid]\n", argv [0]);
		exit(EXIT_FAILURE);
	
	}

	return 0;
}


