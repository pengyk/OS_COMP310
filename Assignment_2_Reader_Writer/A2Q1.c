#include <stdlib.h>
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>

static int glob = 0;
static sem_t mutex;
static sem_t rw_mutex;
static int read_count=0;
double readMin, readMax, readAverage, writeMin, writeMax, writeAverage, writeAccum, readAccum;



void *writeFunction(void *arg) {
	//timespec to calculate the starting and stoping times
	struct timespec startWrite, stopWrite;
	int numToWrite = *((int *) arg);
	//for loop the number of time to try
	for(int i = 0; i < numToWrite; i++){
		//get start time of write
		if( clock_gettime(CLOCK_REALTIME, &startWrite) == -1 ) {
			perror( "clock gettime" );
			exit( EXIT_FAILURE );
		}
		if(sem_wait(&rw_mutex)==-1)
			exit(2);
		if( clock_gettime( CLOCK_REALTIME, &stopWrite) == -1 ) {
			perror( "clock gettime" );
			exit( EXIT_FAILURE );
		}
		//get time in nano seconds
		double currentTime = (stopWrite.tv_sec - startWrite.tv_sec) * 1e9 + (stopWrite.tv_nsec - startWrite.tv_nsec);
		//set the min and max times
		if(writeMin == 0){
			writeMin = currentTime;
		}else if(writeMin > currentTime){
			writeMin = currentTime;
		}
		if(writeMax == 0) {
			writeMax = currentTime;
		}else if (writeMax < currentTime){
			writeMax = currentTime;
		}
		//store the sum of all the time
		writeAccum += currentTime;
		//perform write
		int local = glob;
		local = local + 10;
		glob = local;
		if(sem_post(&rw_mutex)==-1){
			exit(2);
		}
		//sleep for random amount of time
		int n = rand()%101;
		usleep(n*1e3);
	}
	return NULL;
}

void *readFunction(void *arg) {
	//timespec to calculate the starting and stoping times
	struct timespec startRead, stopRead;
	int numToRead = *((int *) arg);
	//for loop the number of time to try
	for(int i = 0; i< numToRead; i++){
		//get start time
		if( clock_gettime( CLOCK_REALTIME, &startRead) == -1 ) {
			perror( "clock gettime" );
			exit( EXIT_FAILURE );
		}
		if(sem_wait(&mutex)==-1)
			exit(2);
		read_count++;
		if(read_count==1){
			if(sem_wait(&rw_mutex)==-1)
				exit(2);
		}
		if(sem_post(&mutex)==-1)
			exit(2);
		//get end time
		if( clock_gettime( CLOCK_REALTIME, &stopRead) == -1 ) {
			perror( "clock gettime" );
			exit( EXIT_FAILURE );
		}
		//get time in nano seconds
		double read_Time = (stopRead.tv_sec - startRead.tv_sec) * 1e9 +(stopRead.tv_nsec - startRead.tv_nsec);
		//set the min and max times
		if(readMin == 0){
			readMin = read_Time;
		}else if(readMin > read_Time){
			readMin = read_Time;
		}
		if(readMax == 0) {
			readMax = read_Time;
		}else if (readMax < read_Time){
			readMax = read_Time;
		}
		//store the sum of all the time
		readAccum += read_Time;
		//perform read
		int local = glob;
		//additional sleep to simulate read
		usleep(2);
		if(sem_wait(&mutex)==-1)
			exit(2);
		read_count--;
		if (read_count == 0){
			if(sem_post(&rw_mutex)==-1)
				exit(2);
		}
		if(sem_post(&mutex)==-1)
			exit(2);
		//sleep for random amount of time
		int n = rand()%101;
		usleep (n*1e3);
	}
	return NULL;
}

int main(int argc, char *argv[]) {
	//the 500 and 10 threads used
	pthread_t readThreads[500];
	pthread_t writeThreads[10];
	//the read count
	int read_count = 0;
	//used to check if there is an error
	int s;

	if(argc != 3){
        printf("Incorrect number of arguments\n");
        exit(1);
    }
	//inputs number of times to try to write and read

	int numToWrite = atoi(argv[1]);
	int numToRead = atoi(argv[2]);

	printf("Num to read: %d\n", numToRead);
	printf("Num to Write: %d\n", numToWrite);

  //set mutex to 1
	if (sem_init(&mutex, 0, 1) == -1) {
		printf("Error, init semaphore\n");
		exit(1);
	}

  //set rw mutex to 1
	if (sem_init(&rw_mutex, 0, 1) == -1) {
		printf("Error, init semaphore\n");
		exit(1);
	}


	for(int i = 0; i < 10; i++) {
		s = pthread_create(&writeThreads[i], NULL, writeFunction, &numToWrite);
		if (s != 0) {
			printf("Error, creating threads\n");
			exit(1);
		}
	}
	for(int i = 0; i < 500; i++) {
		s = pthread_create(&readThreads[i], NULL, readFunction, &numToRead);
		if (s != 0) {
			printf("Error, creating threads\n");
			exit(1);
		}
	}
	for(int i=0; i < 10; i++){
		s = pthread_join( writeThreads[i], NULL); 
		if (s != 0) {
			printf("Error, joining threads\n");
			exit(1);
		}
	}
	for(int i=0; i < 500; i++){
		s = pthread_join(readThreads[i], NULL); 
		if (s != 0) {
			printf("Error, joining threads\n");
			exit(1);
		}
	}
	writeAverage = writeAccum/(numToWrite*10);
	readAverage = readAccum/(numToRead*500);
	printf("\n");
    printf("Min write: %f ns\n", writeMin);
    printf("Min read: %f ns\n", readMin);
    printf("\n");
    printf("Average write: %f ns\n", writeAverage);
    printf("Average read: %f ns\n", readAverage);
    printf("\n");
    printf("Max write: %f ns\n", writeMax);
    printf("Max read: %f ns\n", readMax);
    printf("\n");
	return 0;
}