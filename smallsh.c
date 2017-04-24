#/*
 * Author: Grace Thompson
 *
 * Description: This program is a basic C shell, that contains three 
 * built-in commands, exit, cd, and status. The shell allows for redirection
 * of standard input and standard output, and supports both foreground
 * and background processes. Comment lines that start with # will be ignored,
 * and will run commands input from the user from the command line.
 */

#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h> 
#include <fcntl.h>
#include <signal.h>

//define max for buffer and arg list
#define MAXBUFSIZE 2048
#define MAXARGS 512
#define MAXFORK 100 //to keep forks from overloading the server

//global variable that the status command retrieves
static int STATUSCODE;
static int NUMFORKS = 0; //init at 0, to compare to MAXFORK
pid_t backgroundProcesses[10] = { 0 }; //stores pid of background processes, init to invalid value
static int NUMBKGRND = 0; //keeps count of pids in backgroundProcesses

//this function adds the pid of a background process to the next available index in backgroundProcesses array
void addBackgroundProc(pid_t pid) {
	backgroundProcesses[NUMBKGRND] = pid;
	NUMBKGRND++;
//	printf("backgroundProcess added pid: %d\n", backgroundProcesses[NUMBKGRND - 1]);
}


//this function reads in a single line from standard input (command line)
//and returns a pointer to the string, stored in line. 
char * readLine() {
	//pointer to array of chars that stores the line being read
	char * line = (char *)malloc(sizeof(char) * MAXBUFSIZE);
	//make sure allocation was successful
	if (!line) {
		fprintf(stderr, "smallsh: readLine allocation failed.\n");
		fflush(stderr);
		//exit with failure
		exit(EXIT_FAILURE);
	}
	//get line from user command line
	char buffer[MAXBUFSIZE]; 
	if (fgets(buffer, MAXBUFSIZE, stdin) != NULL) {
		strcpy(line, buffer);
	}
	//clear the buffer
	strcpy(buffer, "");	
	//return the whole line for processing
	return line;
}

//this function takes a string pointer as a parameter
//and uses the strtok function to separate each word and
//add to the string array containing all the arguments. 
//delimiter is the space, does not work with quotes.
char ** parseLine(char *line) {
	//delimiters for tokenizing
	char delimiters[] = " \t\r\n";
	//max number of arguments 
	int maxArgs = MAXARGS;
	//array to hold the tokens, allocated memory
	char ** arguments = malloc(maxArgs * sizeof(char*));
	char * arg; //holds single token, one argument at a time.
	int pos = 0; //stores next available position to add arg to arguments

	//check to make sure allocation worked for arguments
	if (!arguments) {
		fprintf(stderr, "smallsh: parseLine allocation error\n");
		fflush(stderr);
		exit(EXIT_FAILURE); //exit with error
	} else {
		//grab first token
		arg = strtok(line, delimiters);
		//while not null, add token to arguments array
		while (arg != NULL) {
			//add to arguments array
			arguments[pos] = arg;
			pos++; //increment pos to point to next available spot
			//go to next token, repeat until token is NULL
			arg = strtok(NULL, delimiters);
		}
		//set last position to NULL to indicate the end of the arguments
		arguments[pos] = NULL;
		return arguments; //now has all the tokens added.
	}
}

//this function changes directory to either the user specified directory,
//or if not specified, sends to HOME directory
int cdCommand(char *dir) {
	//check if passed param is not null
	if (dir != NULL) {
		//try and change directory
		//if the chdir fails, returns any number other than 0, send error message
		if(chdir(dir) != 0) {
			perror(dir);
			return 1;
		} else {
			return 0;
		}
	}else {
		//change directory to HOME environment.
		const char *home = "HOME";
		char *value;
		value = getenv(home);
		if(chdir(value) != 0) {
			perror(home);
			return 1;
		} else {
			return 0;
		}
	}
}

//this function returns the value stored in STATUSCOMMAND, which holds the
//status of the last finished child process (in the foreground).
//If no exit status, print out terminating signal
void statusCommand(int statusCode) {
	//if exit failed or succeeded
	if (statusCode == 0 || statusCode == 1) {
		fprintf(stdout, "exit value  %i\n", statusCode);
		fflush(stdout);
	} else {
		fprintf(stdout, "terminated by signal %i\n", statusCode);
		fflush(stdout);
	}
}

//this function executes the user command that's not a built-in.
//runCommand takes two parameters, the array of arguments and the
//number of arguments in the array.
//checks for background/foreground status and redirection
//of input and output.
int runCommand(char ** args, int argNum) {

	pid_t pid, wpid; //stores pid from fork, and parent wait pid
	int status;
	int background; //indicates foreground process

	char inputFile[100]; //store name of file for redirection of input 
	char outputFile[100];//same, for output
	int fdInput;
	int fdOutput; //file descriptors for redirection
	int in = 0;
	int out = 0;

	//check against MAXFORKS to make sure we are not overloading the server
	if (NUMFORKS >= MAXFORK) {
		printf("Forks reached maximum, need to exit\n");
		return 1;
	}

	//fork off to create child process
	pid = fork();
	//updte NUMFORKS counter
	NUMFORKS++;
	//check if child process, parent process, or error
	if (pid == 0) {
		//child process
//		printf("I am the child process!\n");
		//check if process is to be executed in the background
		if (strcmp(args[argNum - 1], "&") == 0) {
			//set argument to NULL so it's not passed to exec
			args[argNum -1] = '\0';
			background = 1;
			//set signal handler to ignore Ctrl-C
			signal(SIGINT, SIG_IGN);

			//print process id of background process
			pid_t childPid = getpid();
			fprintf(stdout, "background pid is %i\n", childPid);
			fflush(stdout);
	
		}else {
			//process is to be run in the foreground, Ctrl-C should terminate only this process
			signal(SIGINT, SIG_DFL);
		}
		//parse through arguments for redirection
		int i;
		for(i = 0; args[i] != '\0'; i++) {
			if(strcmp(args[i], "<") == 0) {
				//found input redirection character
				//set value to NULL so we don't pass this to exec
				args[i] = NULL;
				strcpy(inputFile, args[i+1]); //get name of input file
				//set input flag
				in = 1;

			}else if(strcmp(args[i], ">") == 0) {
				//found output redirection
				args[i] = NULL;
				strcpy(outputFile, args[i+1]);
				out = 1;
				
			}
		}
		//redirect input
		if (in == 1) {
			//try to open inputFile
			fdInput = open(inputFile, O_RDONLY, 0777);
			if (fdInput < 0) {
				//file could not be opened
				fprintf(stderr, "cannot open %s for input\n", inputFile);
				fflush(stderr);
				exit(1);
			}
			//redirection of input
			dup2(fdInput, 0);
			//close file
			close(fdInput);
		}
		//redirect output
		if (out == 1) {
			fdOutput = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0777);
			if (fdOutput < 0) {
				fprintf(stderr, "cannot open %s for output\n", outputFile);
				fflush(stderr);
				exit(1);
			}
			//redirection of output
			dup2(fdOutput, 1);
			//close file
			close(fdOutput);
		}

		//if background process, redirect to /dev/null if no file specified
		if (background == 1) {
			//check if no input file specified
			if (in != 1) {
				fdInput = open("/dev/null", O_RDONLY, 777);
				if (fdInput < 0) {
					fprintf(stderr, "cannot open /dev/null for input\n");
					fflush(stderr);
					exit(1);
				}
				dup2(fdInput, 0);
				close(fdInput);
			}
			//if no output file specified, redirect to /dev/null
			if (out != 1) {
				fdOutput = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 777);
				if (fdOutput < 0) {
					fprintf(stderr, "cannot open /dev/null for output\n");
					fflush(stderr);
					exit(1);
				}
				dup2(fdOutput, 1);
				close(fdOutput);
			}
		}
		//start new process, check if failed (no return on success)
		if (execvp(args[0], args) == -1) {
			//exec failed
			perror(args[0]);
			exit(EXIT_FAILURE);
		}
		
		//if we get here, there was an error
		return 1;
	} else if (pid < 0) {
		//error with the forking
		perror("smallsh");
		return 1; //send back error for STATUSCODE
	} else {
		//parent process. if foreground process, wait for child to finish
		if (strcmp(args[argNum - 1], "&") != 0) {
			//wait until the child process is completed or terminated by some signal
			do {
				wpid = waitpid(pid, &status, WUNTRACED);
			} while (!WIFEXITED(status) && !WIFSIGNALED(status));
			//when child is finished, update count of NUMFORKS
			NUMFORKS--;
			//check status if child terminated normally
			if (WIFEXITED(status)) {
				return WEXITSTATUS(status);
			}else if (WIFSIGNALED(status)) {
				//if child was terminated by a signal
				printf("terminated by signal: %d\n", WTERMSIG(status));
				return WTERMSIG(status);
			}else if (WIFSTOPPED(status)) {
				printf("stopped by delivery of signal: %d\n", WSTOPSIG(status));
				return WSTOPSIG(status);
			}
		} else {
			//add child process to backgroundProcesses array
			addBackgroundProc(pid);
		//	printf("child process id: %d\n", pid);
		//	fflush(stdout);
			return 0;
		}
		
	}		
}


//this function examines the arguments input by the user, and checks
//for built-in commands exit, cd, and status. If the argument is none of these,
//function must execute command given. If first argument begins with #, ignore.
int executeCommand(char ** args, int argNum) {
	char *arg;
	int status;
	//check if first argument is NULL somehow
	if (args[0] == NULL) {
		//emtpy command, return 1 to keep looping
		return 1;
	} else {
		//check first command, if starts with #, return 1
		arg = args[0]; //grab first argument
	//	printf("arg[0]: %c \n", arg[0]);
		//check if first char of argument is a comment symbol
		if(arg[0] == '#') {
			return 1; //keep looping, ignore comment line
		}
		//compare first argument to built-in commands
		if (strcmp("exit", arg) == 0) {
			//kill any processes
			//check for any active background processes
			int i;
			for (i = 0; i < 10; i++) {
				if (backgroundProcesses[i] != 0) {
					kill(backgroundProcesses[i], SIGTERM);
				}
			}
		
			return 0; //exit out of loop
		} else if (strcmp(arg, "cd") == 0) {
			//execute with next argument, even if null (will send to HOME)
			status = cdCommand(args[1]); 	
	//		printf("status: %i \n", status);
			//check result of cd command
			if (status == 0) {
				STATUSCODE = 0;
			}else if (status == 1) {
				STATUSCODE = 1;
			}
	//		printf("STATUSCODE: %i \n", STATUSCODE);
			//keep loop running
			return 1;
		} else if (strcmp("status", arg) == 0) {
			statusCommand(STATUSCODE);
			return 1;
		} else {
			//run command input by user
			status = runCommand(args, argNum);
			//store status of completed process
		 	STATUSCODE = status;
	//		printf("STATUSCODE after runCommand: %i \n", STATUSCODE);
			return 1;
		}
	}	
		//shouldn't actually make it this far.
		return 1;
}

void shellLoop() {
	char **args; //array stores arguments from command line
	char *line; //whole line grabbed off command line
	int loop; //determines when to exit loop
	int len; //length of string read in
	pid_t bkpid; //to get pid of background process when it finishes
	int status; //holds status of background process once finished.


	//signal handler for interrupt signal
	struct sigaction act;
	act.sa_handler = SIG_IGN; //should ignore SIGINT signal, should not cause shell to terminate.
	act.sa_flags = SA_RESTART; //so function doesn't try and pass the Ctrl-C as an argument
	sigfillset(&(act.sa_mask)); //make sure no other signals interrupt the handler
	
	sigaction(SIGINT, &act, NULL); //handler for SIGINT interrupt signals

	int i;
	do {
		//check if any background processes have completed
		for (i = 0; i < 10; i++) {
			if (backgroundProcesses[i] != 0) {
		//		printf("backgroundProcesses[%i] = %i\n", i, backgroundProcesses[i]);
				//if process completed, bkpid will hold the pid of the process.
				//if still going, returns 0.
				bkpid = waitpid(backgroundProcesses[i], &status, WNOHANG);
				//if process exited normally
				if (WIFEXITED(status) && bkpid != 0) {
					fprintf(stdout, "background pid %d is done: exit value %i\n", bkpid, WEXITSTATUS(status));
					fflush(stdout);
					//set status code
					STATUSCODE = WEXITSTATUS(status);
					//set value at index to -1 since completed
					backgroundProcesses[i] = 0;
					//update count
					NUMBKGRND--;
				//if process was terminated by a signal
				}else if (WIFSIGNALED(status) && bkpid != 0) {
					fprintf(stdout, "background pid %d is done: terminated by signal %i\n", bkpid, WTERMSIG(status));
					fflush(stdout);
					//set status code
					STATUSCODE = WTERMSIG(status);
					backgroundProcesses[i] = 0;
					NUMBKGRND--;
				}
			}
		}	

		printf(": "); //prompt symbol
		//read in line from command line
		line = readLine();
	//	printf("line: %s \n", line);
		//parse line into separate arguments
		args = parseLine(line);	
		//count arguments
		int count = 0;
		int i = 0;
		while (args[i] != NULL) {
//			printf("%s \t", args[i]);
			i++;
			count++;
		}
		
		//get first argument, check if \n char
		char *firstArg = args[0];
//		printf("firstArg char: %c \n", firstArg[0]);
		//if only one argument, NULL, loop again
		if(firstArg[0] == '\n') {
			loop = 1;
		} else {
			//send arguments to be executed
			//will either continue the loop
			//or cause it to exit.
			//pass count so function knows how many arguments there are
			loop = executeCommand(args, count);
		}

		//free memory allocated for line and args
		free(line);		
		free(args);
	} while (loop == 1);
}


int main() {
	//loop prompt for command line
	shellLoop(); 

	return 0; //exit successfully
}
