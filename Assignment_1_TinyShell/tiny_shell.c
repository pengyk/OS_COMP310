#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

//history 2d array
char history [100][100];
//j is counter for number of entry in history
int j = 0;
struct rlimit rlim;
//a copy of the input to use in a pipe
static char fifoArray[100];
//the path of the fifo
char * fifoPath;

char* get_a_line() {
	//fct to get the line from user input and file
	int i=0;
	static char buffer[100];
	char c = getchar();
	while(1){
		if(c=='\n'){
			break;
		}else if(c==EOF){
			exit(0);
		}else{
			buffer[i]=c;
			i++;
			c = getchar();
		}
	}
	buffer[i]='\0';
	return buffer;
}


int my_system(char* line) {
	//check the content of the line, the command
	printf("Command : %s\n", line);
	int k = 0;
	//history pointer to store the content to the history pointer
	char* histrPtr = line;

	//pointer for the fifo in order to store the whole content of each new line of command
	char* fifoPtr = line;
	int a=0;
	while(*fifoPtr != '\0'){
		fifoArray[a] = *fifoPtr;
		fifoPtr++;
		a++;
	}
	fifoArray[a] = '\0';

	//copy each line into one array in the 2d array history in order to save all commands
	for(k = 0; k < 100; k++){
		if(*histrPtr == '\0'){
			history[j][k] = '\0';
			break;
		}else{
			history[j][k] = *histrPtr;
			histrPtr++;
		}
	}
	//only increment once per line
	j++;

	//basic initialization and tokenization
	char bin[100] = "/bin/";
	char *token;

	//argVV is the value returned in tokenized form of the command
	char* argVV[100];
	token = strtok(line, " ");
	int i = 0;
	//tokenize
	while( token != NULL ) {
		argVV[i] = token;
		i++;
		token = strtok(NULL," ");
	}
	argVV[i] = NULL;
	//copy the command to /bin/
	strcpy(bin+strlen(bin), argVV[0]);

	

	//chdir or cd command executed here
	if (strcmp(argVV[0], "chdir") == 0 || strcmp(argVV[0], "cd") == 0){
		if( chdir( argVV[1] ) == 0 ) {
			printf( "Directory changed to %s\n", argVV[1] );
			return 0;
		} else {
			perror( argVV[1] );
			return -1;
		}
		//history check here, loop thorugh 2d array and print everything
	}else if (strcmp(argVV[0], "history") == 0) {
		for(int c=0; c<100; c++) {
			if(*history[c] == '\0'){
				//break;
				return 0;
			}else{
				printf("  %d.  %s\n",(c+1), history[c]);
			}
		}
			// printf("%d\n", histCounter);
		return 0;
	}else if(strcmp(argVV[0], "limit") == 0){
		if(atoi(argVV[1]) == '0'){
			printf("Wrong type of argument to limit\n");
			return 0;
		}else{
			getrlimit(RLIMIT_DATA, &rlim);
			printf("Old soft limit is : %lld\n", (long long int)rlim.rlim_cur); 
			printf("Old hard limit is : %lld\n", (long long int)rlim.rlim_max);
			int limitNbr = atoi(argVV[1]);
			printf("Limit Nbr: %d\n", limitNbr);
			rlim.rlim_cur = limitNbr;
			rlim.rlim_max = limitNbr;
			if(setrlimit(RLIMIT_DATA, &rlim) == -1) {
				printf("rlimit failed\n");
				return 0;
			}else{
				getrlimit(RLIMIT_DATA, &rlim);
				printf("New soft limit is : %lld\n", (long long int)rlim.rlim_cur); 
				printf("New hard limit is : %lld\n", (long long int)rlim.rlim_max);
				return 0;
			}
		}
	}
	pid_t pid = fork();

	if (pid < 0) {
		//error
		fprintf(stderr, "Fork Failed");
		return -1;
	}else if(pid == 0) {
		//if it is a child process
		for(int l = 0; l < i; l++) {
		//if it is a pipe
			if(strcmp(argVV[l], "|") == 0) {
				//set the pipe limit
				int pipeLimit = l;
				//get first command (everything before the limit)
				char* token1;
				char* argFirstCom[100];
				char *fifoLinePtr = fifoArray;
				token1 = strtok(fifoLinePtr, " ");
				int i = 0;
				//tokenize first argument of the piped command//
				while( token1 != NULL ) {
					
					argFirstCom[i] = token1;
					//if it hits the pipe, break
					if(strcmp(argFirstCom[i], "|")==0){
						break;
					}
					i++;
					token1 = strtok(NULL," ");
				}
				//replace the "|" with null
				argFirstCom[i] = NULL;		
			//create another string of /bin/ as bin2
				char bin2[100] = "/bin/";
			//copy the fist command to bin2
				strcpy(bin2+strlen(bin2), argFirstCom[0]);
				////////////////////////////////////////////////////
				///print the command
				pid_t pid1 = fork();
				int fd;
				if (pid1 < 0) {
					//error
					fprintf(stderr, "Fork Failed");
					return -1;
				}else if(pid1 == 0) {

				//child process of wordcount
					if(strcmp(argFirstCom[0], "wc") == 0) {
						
						char usrBin2[100] ="/usr/bin/wc";
					//close stdout
						//close(1);
						fd = open (fifoPath, O_WRONLY);
					//replacing stdout with fd
						dup2(fd, 1);
					//closing fd
						close(fd);
					//execute the command
						execvp(usrBin2, argFirstCom);
					
					}
				//create child process internally
					else if(access(bin2, F_OK) != -1){
						int fd;
					//close stdout
						//close(1);
						fd = open (fifoPath, O_WRONLY);
					//replacing stdout with fd
						dup2(fd, 1);
					//closing fd
						close(fd);
					//execute the command
						execvp(bin2, argFirstCom);
					
					}else{
						printf("Wrong command: %s\n", bin);
						exit(0);
					}
				}
				
				//second element of piped element
				pid_t pid2 = fork();
				int m;	
				char bin3[100] = "/bin/";
				// only copy the first element after the pipe of the initial input
				strcpy(bin3+strlen(bin3), argVV[pipeLimit+1]);

				if (pid2 < 0) {
			//error
					fprintf(stderr, "Fork Failed");
					return -1;
				}else if(pid2 == 0) {

				//child process of wordcount
					if(strcmp(argVV[pipeLimit+1], "wc") == 0) {
						
						char usrBin2[100] ="/usr/bin/wc";
					//close stdin
						//close(0);
						fd = open (fifoPath, O_RDONLY);
					//replacing stdout with fd
						dup2(fd, 0);
					//closing fd
						close(fd);
					//execute the command
						execvp(usrBin2, argVV+pipeLimit+1);
						_exit(0);
					}
				//create child process internally
					else if(access(bin3, F_OK) != -1){
						//close(0);
						fd = open (fifoPath, O_RDONLY);
					//replacing stdout with fd
						dup2(fd, 0);
					//closing fd
						close(fd);
					//execute the command
						execvp(argVV[pipeLimit+1], argVV+pipeLimit+1);
						_exit(0);
					}else{
						printf("Wrong command: %s\n", bin);
						exit(0);
					}
				}
				wait(NULL);
				_exit(0);
			}

		}
		//hits this line if it is not a pipe command
		//if it is a wordcount, need to acces different path
		if(strcmp(argVV[0], "wc") == 0) {
			char usrBin[100] ="/usr/bin/wc";
			execvp(usrBin, argVV);
			_exit(0);
		}
		//create child process
		if(access(bin, F_OK) != -1){
			execvp(bin, argVV);
			_exit(0);
		}else{
			printf("Wrong command: %s\n", bin);
			exit(0);
		}
	}else{
		//in parent since process ID positive
		//try to find if it is a pipe "|"
		wait(NULL);
		return 0;
	}
		//printf("Child Complete\n");
		//printf("jai bientot termine mon tp!!\n");
}
	// printf("%s\n", line);
void intHandler(int dummy) {
	printf("\nAbout to head out? (y/n)\n");
	char answer;
	read(0, &answer, 1);
	fflush(stdout);
	if(answer == 'y' || answer == 'Y'){
		printf("Got out\n");
		exit(0);
	}
	printf("Still in\n");
	//break;
}


void zIntHandler(int signnum) {
	signal(SIGTSTP, zIntHandler); 
	printf("\nCannot be terminated using Ctrl+Z \n"); 
	fflush(stdout); 
}


int main(int argc, char const *argv[])
{
	signal(SIGINT, intHandler);
	signal(SIGTSTP, zIntHandler);
	if(argc >=2) {
		char absoluteFifo[100];
		fifoPath = realpath(argv[1], absoluteFifo);
	}
	//fifoPath = argv[1];
	while (1) {
		//signal(SIGINT, intHandler);
		//fifoPath = argv[1];
		char *line = get_a_line();
		if (strlen(line) > 1) {
			my_system(line);
		}
	}
	return 0;
}