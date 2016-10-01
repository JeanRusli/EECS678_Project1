/**
 * @file quash.c
 *
 * Quash's main file
 */

/**************************************************************************
 * Included Files
 **************************************************************************/ 
#define _POSIX_SOURCE
#define _GNU_SOURCE
#include "quash.h" // Putting this above the other includes allows us to ensure
                   // this file's headder's #include statements are self
                   // contained.

#include <signal.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>


/**************************************************************************
 * Private Variables
 **************************************************************************/
/**
 * Keep track of whether Quash should request another command or not.
 */
// NOTE: "static" causes the "running" variable to only be declared in this
// compilation unit (this file and all files that include it). This is similar
// to private in other languages.
static bool running;

extern char** environ;		// Used to search for executables.
static char *PATH = NULL;	// Path to search for executables.
static char *HOME = NULL;	// User's Home directory.
char* path = NULL;		// Used to set up environment.
char* home = NULL;		// Used to set up environment.
char* p = "PATH";
char* h = "HOME";

static int numArgs = 0;		// Number of arguments inserted in 1 command line.	
char** argsa;			// Used to parse arguments in command line.

// Size and list of built-in functions.
#define sizeBuiltIn (8)
char *builtIn[] = {"set", "echo", "cd", "pwd", "quit", "exit", "jobs","kill"};

//=========================================================================
//	Custom typedef for jobs (background process).
//=========================================================================
typedef struct jobs{
  int jobID;			// Job ID
  pid_t jobPID;			// Job PID
  char* program;		// Command
  struct jobs* next;		// Pointer to next job.
} job;

static job* root = NULL;	// Initialize root to NULL (no jobs yet).
static int numOfJobs;		// Number of jobs/background process currently running.


/**************************************************************************
 * Private Functions 
 **************************************************************************/
/**
 * Start the main loop by setting the running flag to true.
 */
static void start() {

  numOfJobs = 0;

  // Initialize default values for PATH and HOME.
  PATH = (char*)malloc(strlen(getenv(p)) * sizeof(char*));
  strcpy(PATH,getenv(p));
  HOME = (char*)malloc(strlen(getenv(h)) * sizeof(char*));
  strcpy(HOME,getenv(h));

  // Set the state of shell.
  running = true;
}


/**************************************************************************
 * Public Functions 
 **************************************************************************/
bool is_running() {
  return running;
}

/**
 * Stop and exit program by deallocating all the used memory and setting
 * running flag to be false.
 */
void terminate() {

  // Deallocate all variables used to set up PATH and HOME.
  if(PATH != NULL) {
    free(PATH);
    PATH = NULL;
  }
  if(HOME != NULL) {
    free(HOME);
    HOME = NULL;
  }

  if(path != NULL) {
    free(path);
    path = NULL;
  }
  if(home != NULL) {
    free(home);
    home = NULL;
  }

  // Deallocate variables used to set up jobs/background process.
  if(root != NULL) {
    job* tmp = root;
    while(root != NULL) {
      tmp = root->next;
      free(root->program);
      free(root);
      root = NULL;
      root = tmp;
    }
    root = NULL;
  }

  // Stop main's while loop.
  running = false;
}

/**
 * Method to get new command from STDIN.
 */
bool get_command(command_t* cmd, FILE* in) {
  if (fgets(cmd->cmdstr, MAX_COMMAND_LENGTH, in) != NULL) {
    size_t len = strlen(cmd->cmdstr);
    char last_char = cmd->cmdstr[len - 1];

    if (last_char == '\n' || last_char == '\r') {
      // Remove trailing new line character.
      cmd->cmdstr[len - 1] = '\0';
      cmd->cmdlen = len - 1;
    }
    else
      cmd->cmdlen = len;

    return true;
  }
  else
    return false;
}

/**
 * Method to update PATH and HOME values.
 * Parameter args is the command line that is used to get
 * the new values for PATH or HOME.
 * Format = set [PATH/HOME]=[new values]
 * Returns an integer value upon exiting.
 */
int setQuash(char **args)
{
  // if-else statements to check whether input has the valid format.
  // The first argument should specify whether user is trying to update
  // PATH or HOME's value.
  if(args[1] == NULL) {
    fprintf(stderr, "setQuash: expected argument to \"set\"\n");

  } else {
    // The second argument should have equal sign ("=").
    if(args[2] == NULL) {
      fprintf(stderr,"setQuash: Syntax error\n");

    } else {
      if(strcmp(args[2],"=") != 0) {
        fprintf(stderr,"setQuash: Syntax error\n");

      } else {
        // New value for PATH or HOME needs to be specified as the third argument.
        if(args[3] == NULL){
          fprintf(stderr,"setQuash: expected new value\n");

        } else {
          // Update PATH.
          if (strcmp(args[1], "PATH") == 0) {
            
            if(PATH != NULL) {
              free(PATH);
              PATH = NULL;
            }

            PATH = (char*)malloc(sizeof(char*) * strlen(args[3]));
            bzero(PATH,sizeof(PATH));
            strcpy(PATH,args[3]);
	          setenv(p,PATH,1);

          // Update HOME.
          } else if (strcmp(args[1], "HOME") == 0) {
            
            if(HOME != NULL) {
              free(HOME);
              HOME = NULL;
            }

            HOME = (char*)malloc(sizeof(char*) * strlen(args[3]));
            bzero(HOME,sizeof(HOME));
            strcpy(HOME,args[3]);
	          setenv(h,HOME,1);

          } else {
            perror("setQuash");
          }
        }
      }
    }
  }

  return 1;
}

/**
 * Method to print out PATH and HOME contents or other messages.
 * Parameter args is the command line that is used to know
 * what to print out.
 * Format = echo $[PATH/HOME] or echo [message]
 * Returns an integer value upon exiting.
 */
int echoQuash(char **args)
{
  // if-else statements to check whether input has the valid format.
  // The first argument should specify what user is trying to print out.
  if (args[1] == '\0') {
    fprintf(stderr, "Quash: expected argument to \"echo\"\n");

  } else {
    // Print out PATH.
    if (strcmp(args[1], "$PATH") == 0) {       
      printf("%s\n", PATH);

    // Print out HOME.
    } else if (strcmp(args[1], "$HOME") == 0) {
      printf("%s\n", HOME);

    // Print out message.
    } else {
      int i;
      for(i = 1; i < numArgs; i++){
        printf("%s ",args[i]);
      }
      printf("\n");
    }
  }

  return 1;
}

/**
 * Method to change the current working directory.
 * Parameter args is the command line that is used to know
 * the target directory. If there's no target given, change it
 * to HOME variable.
 * Format = cd [target directory]
 * Returns an integer value upon exiting.
 */
int cdQuash(char **args)
{
  // if-else statements to check the input's format.
  // The first argument is used specify the new directory.
  // If there's no directory specified, change the directory to HOME.
  if (args[1] == NULL) {
    if (chdir(HOME) != 0) {
      perror("cdQuash");
    }

  } else {
    if (chdir(args[1]) != 0) {
      perror("cdQuash");
    }
  }

  return 1;
}

/**
 * Method to print the absolute path of current working directory.
 * Parameter args is the command line.
 * Returns an integer value upon exiting.
 */
int pwdQuash(char **args)
{
  // Used to store the value of current working directory.
  char cwd[1024];	

  // Get and print out current working directory.
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\n", cwd);
  } else {
    perror("pwdQuash");
  }

  return 1;
}

/**
 * Method to print all background processes that are currently running.
 * Result = "[JOBID] PID COMMAND"
 * Returns an integer value upon exiting.
 */
int jobs() {
  // if-else statement to know whether there is any job that can be printed out.
  if(root == NULL) {
    printf("No active job.\n");

  } else {
    job* tmp = root;
    // while-loop to print out all jobs.
    while(tmp != NULL) {
      printf("[%d] %d %s\n",tmp->jobID,tmp->jobPID,tmp->program);
      tmp = tmp->next;
    }
  }

  return 1;
}

/**
 * Method to send a signal to the process that has JOBID
 * specified in the command. It returns 0 if it fails,
 * and non-zero otherwise. 
 * Syntax: kill SIGNUM JOBID
 * Note: SIGNUM must be in the integer form of the signal.
 */
int killQuash(char** args) {

  // Syntax error, number of argument must be at least 3.
  if(numArgs < 3) {
    printf("Quash_kill: Syntax Error\n");
    return 0;
  } else {

    // Convert JOBID and SIGNUM to integers.
    int jid = atoi(args[2]);
    int signum = atoi(args[1]);
    pid_t pid;
    job* tmp = root;
    bool jidFound = false;

    // Search for JOBID.
    while(tmp != NULL) {
      if(jid == tmp->jobID) {
        pid = tmp->jobPID;
        jidFound = true;
        break;
      } else {
        tmp = tmp->next;
      }
    }

    // Send signal to the PID of the JOB found or notify
    // the user if it is not found.
    if(jidFound) {
      printf("Sending Signal\n");
      return kill(pid, signum);
    } else {
      printf("Job is not Found\n");
      return 0;
    }

  }
}

/**
 * Method to split the commands into two strings
 * according to the syntax specified.
 * The cmd string will then contains the second string
 * value.
 * Returns a char* to the first string upon exiting.
 * Note: Whitespace characters are '\0', '\t', '\n', '\r', ' '
 *	 Special characters are '|', '&', '=', '<', '>'
 */
char* get_token(char* cmd) {
  int size = strlen(cmd);
  int i = 0;
  int j = 0;
  int pos = 0;
  bool inQuote = false;
  bool inDQuote = false;
  char* tmp;

  // If size is 1, return whatever that is as long as it's not a whitespace character, set cmd to NULL.
  if(size == 1) {
    if((cmd[0] != '\0') && (cmd[0] != '\t') && (cmd[0] != '\n') && (cmd[0] != '\r') && (cmd[0] != ' ')) {
      tmp = (char*)malloc(2 * sizeof(char*));
      tmp[0] = cmd[0];
      tmp[1] = '\0';
      cmd[0] = '\0';
      return tmp;
    } else {
      tmp = (char*)malloc(sizeof(char*));
      tmp[0] = '\0';
      cmd[0] = '\0';
      return tmp;
    }
  } else {

    // Get the first position of non-whitespace character.
    while((pos < size) && ((cmd[pos] == '\0') || (cmd[pos] == '\t') || (cmd[pos] == '\n') || (cmd[pos] == '\r') || (cmd[pos] == ' '))) {
      pos++;
      i++;
    }

    // Return null if cmd contains only whitespace characters, set cmd to NULL.
    if(pos == size) {
      tmp = (char*)malloc(sizeof(char*));
      tmp[0] = '\0';
      bzero(cmd, sizeof(cmd));
      return tmp;
    } else {
      // || (cmd[pos] == ':')
      // || (cmd[pos] == '$')
      // Return the special character, remove the special character from cmd.
      if((cmd[pos] == '|') || (cmd[pos] == '&') || (cmd[pos] == '=') || (cmd[pos] == '<') || (cmd[pos] == '>')) {
        tmp = (char*)malloc(2 * sizeof(char*));
        bzero(tmp,sizeof(tmp));
        tmp[0] = cmd[pos];
        tmp[1] = '\0';
        pos++;

	// Shift the cmd string.
        for(j = pos; j < size; j++) {
          cmd[j-pos] = cmd[j];
        }

	// Set the remainding last characters after shifted to NULL.
        for(j = 0; j < pos; j++){
          cmd[size-1-j] = '\0';
        }
        return tmp;
      } else {
        // && (cmd[pos] != ':')
        // && (cmd[pos] != '$')
	// Get the position of first encountered whitespace or special character after 'pos' variable before. 
	// If the substring is within quote or double quote, get the entire substring
	// even if the substring contains whitespace or special characters.
        while((pos<size) && ((cmd[pos] != '\0') && (cmd[pos] != '\t') && (cmd[pos] != '\n') && (cmd[pos] != '\r') && (cmd[pos] != ' ') && (cmd[pos] != '|') && (cmd[pos] != '&') && (cmd[pos] != '=') && (cmd[pos] != '<') && (cmd[pos] != '>') )) {

          if(inDQuote) {
            while((pos<size-1) && (cmd[pos] != '\"')) {
              pos++;
            }
            inDQuote = false;
          } else if(inQuote) {
            while((pos<size-1) && (cmd[pos] != '\'')) {
              pos++;
            }
            inQuote = false;
          } else if(cmd[pos] == '\"') {
            inDQuote = true;
          } else if(cmd[pos] == '\'') {
            inQuote = true;
          }
          pos++;
        }

	// Copy substring found, ignore quote and double quote characters.
        tmp = (char*)malloc((pos+1)*sizeof(char*));
        bzero(tmp, sizeof(tmp));
        int k;
        for(k = 0; k < (pos+1)*sizeof(char*); k++) {
          tmp[k] = '\0';
        }
        for(i = i; i < pos; i++) {
          tmp[i] = '\0';
          if((cmd[i] != '\'') && (cmd[i] != '\"')) {
            tmp[j] = cmd[i];
            j++;
          }
        }

	// Shift the cmd string.
        if(inQuote || inDQuote) {
          pos--;
        }
        for(j = pos; j < size;j++) {
          cmd[j-pos] = cmd[j];
        }

	// Set the remainding last characters after shifted to NULL.
        for(j = 0; j < pos; j++) {
          cmd[size-1-j] = '\0';
        }
        return tmp;
      }
    }
  }
}

/**
 * Method to parse the command. It returns a char** that
 * contains the command that has been broken down into
 * substrings according to the syntax specified. It also
 * sets cmd to NULL, and set the numArgs variable to the
 * number of arguments in the parsed command.
 */
char** parse(char* cmd){
  char** tokens = (char**)malloc(64 * sizeof(char**));
  bzero(tokens, sizeof(tokens));
  numArgs = 0;
  char* tmp;
  if(!tokens) {
    fprintf(stderr,"Quash: allocation error\n");
    exit(EXIT_FAILURE);
  }

  // Put it in the char** variable if returned char* is not NULL.
  tmp = get_token(cmd);
  if(tmp != '\0') {
    tokens[numArgs] = tmp;
    numArgs++;
  }

  // Loop to parse the rest cmd.
  int sizeCMD = strlen(cmd);
  while(sizeCMD > 0) {
    tmp = get_token(cmd);
    sizeCMD = strlen(cmd);

    tokens[numArgs] = tmp;
    numArgs++;
  }

  // If the last char* returned has 0 length, set it to NULL 
  // and decrease numArgs variable.
  if(strlen(tokens[numArgs-1]) == 0) {
    free(tokens[numArgs-1]);
    tokens[numArgs-1] = NULL;
    numArgs--;
  }

  // Set last to NULL.
  tokens[numArgs] = NULL;
  return tokens;

}

/**
 * A signal handler to handle SIGCHLD signal. It detects
 * if a child process has finished (exited) and then delete
 * it from the job list.
 */
void sigchld_handler(int signum){
  int saved_errno = errno;
  pid_t pid;
  int status;

  // Get the PID of the finished child process.
  while((pid = waitpid((pid_t)(-1), &status, WNOHANG)) > 0) {
    job* temp;

    // Notify the user if the process has finished, remove it
    // from the job list. Update the job list.
    if(root->jobPID == pid) {
      temp = root;
      printf("[%d] %d finished %s\n",root->jobID,root->jobPID,root->program);
      root = root->next;
      free(temp->program);
      free(temp);
      temp = root;
      int i = 1;
      while(temp != NULL) {
        temp->jobID = i;
        i++;
        temp = temp->next;
      }
    } else {
      job* find = root;
      while(find->next != NULL) {
        if(find->next->jobPID == pid) {
          temp = find->next;
          find->next = find->next->next;
          printf("[%d] %d finished %s\n",temp->jobID,temp->jobPID,temp->program);
          int jid = temp->jobID;
          free(temp->program);
          free(temp);
          temp = find->next;
          while(temp != NULL) {
            temp->jobID = jid;
            jid++;
            temp = temp->next;
          }
        }
        if(find->next != NULL) {
          find = find->next;
        }
      }
    }
    numOfJobs--;
  }
  errno = saved_errno;
}

/**
 * Method to launch the parsed command. It takes
 * the char** parsed command and the integer numArg
 * as its parameter. 
 * Return 1 if it succeed.
 */
int launch_cmd(char** args, int numArg) {
  pid_t pid, wpid;
  int status;
  if(path != NULL) {
    free(path);
    path = NULL;
  }
  if(home != NULL) {
    free(home);
    home = NULL;
  }

  // Copy the variables to environment variable.
  path = (char*)malloc((strlen(PATH) + 5) * sizeof(char*));
  home = (char*)malloc((strlen(HOME) + 5) * sizeof(char*));

  // Copy PATH and HOME to the environment variable.
  char p1[] = "PATH=";
  char p2[] = "HOME=";
  strcpy(path,p1);
  strcat(path,PATH);
  strcpy(home,p2);
  strcat(home,HOME);
  char *env[]= {
    path,
    home,    
    NULL};
  environ = env;

  // Decide if the command should be executed in background
  // or foreground.
  if(strcmp(args[numArg - 1], "&") == 0) {
    
    pid = fork();

    // Child process.
    if(pid == 0) {
      free(args[numArg -1]);
      args[numArg - 1] = NULL;
      numArg--;
      if(execvpe(args[0],args,environ) == -1) {
        perror("Quash_Launch: ");

	      // Dealocate the command.
        int i;
        for(i = 0; i < numArg; i++) {
          char* tmp = argsa[i];
          free(tmp);
        }
        free(argsa);
        terminate();
      }
      exit(EXIT_FAILURE);
    } else if(pid < 0) {
      perror("Quash_Launch Forking:");
    } else {

      // Parent process.
      // Add the child's pid to the job list.
      int jid = numOfJobs +1;
      numOfJobs++;
      if(root == NULL) {
        root = malloc(sizeof(job));
        root->next = NULL;
        root->jobID = jid;
        root->jobPID = pid;
        root->program = (char*)malloc(strlen(args[0]) * sizeof(char*));
        strcpy(root->program,args[0]);
      } else {
        job* tmp = root;
        while(tmp->next != NULL) {
          tmp = tmp->next;
        }
        tmp->next = malloc(sizeof(job));
        tmp = tmp->next;
        tmp->next = NULL;
        tmp->jobID = jid;
        tmp->jobPID = pid;
        tmp->program = (char*)malloc(strlen(args[0]) * sizeof(char*));
        strcpy(tmp->program,args[0]);
      }

      // Print PID.
      printf("[%d] %d\n",jid,pid);

      if((waitpid(pid,&status,WNOHANG)) == 0) {

      }

    }
  } else {
    pid = fork();

    // Child process.
    if(pid == 0) {
      if(execvpe(args[0],args,environ) == -1) {
        perror("Quash_Launch: ");

        // Dealocate the command.
        int i;
        for(i = 0; i < numArg; i++) {
          char* tmp = argsa[i];
          free(tmp);
        }
        free(argsa);
        terminate();
      }
      exit(EXIT_FAILURE);
    } else if(pid < 0) {
      perror("Quash_Launch Forking:");
    } else {

      // Parent process.
      do {
        // Wait for child process to finish.
        wpid = waitpid(pid, &status, WUNTRACED);
      } while(!WIFEXITED(status) && !WIFSIGNALED(status));
    }
  }
  return 1;    
}

/**
 * Method to redirect a specified file as the standard input.
 * Parameter cmd is the command that is going to be executed.
 * Parameter fileName is the name of file that will be used as input.
 * Parameter numArgsInCmd is the number of arguments in the command
 * line after excluding the redirection arguments.
 * Format = [cmd] < [fileName]
 * Returns an integer value upon exiting.
 */
int fileIn(char*** cmd, char* fileName, int numArgsInCmd[]) {
  // Declare needed variables.
  int fd;			// Used to open file.
  int status;			// Used to wait till child process finishes.
  int stdin_copy = dup(0);	// Used to copy standard input.
  int stdout_copy = dup(1);	// Used to copy standard output.

  // Child process.
  if (fork() == 0) {
    // if statement to know whether the file is opened successfully.
    if ((fd = open(fileName, O_RDONLY, 0644)) < 0) {
      perror(fileName);
      exit(1);
    }

    // Read from file instead of standard input.
    // Close file descriptor and execute command.
    dup2(fd,STDIN_FILENO);
    close(fd);
    launch_cmd(cmd[0],numArgsInCmd[0]);
    exit(0);

  } else {			// Parent process.
    // Close file descriptor and wait till child process finishes.
    close(fd);
    wait(&status);

    // Read from standard input again.
    dup2(stdin_copy, 0);
    dup2(stdout_copy, 1);
    close(stdin_copy);
    close(stdout_copy);
  }

  free(fileName);		// Deallocate fileName.

  return 1;
}

/**
 * Method to redirect the standard output to a specified file.
 * Parameter cmd is the command that is going to be executed.
 * Parameter fileName is the name of file that will be used as output.
 * Parameter numArgsInCmd is the number of arguments in the command
 * line after excluding the redirection arguments.
 * Format = [cmd] > [fileName]
 * Returns an integer value upon exiting.
 */
int fileOut(char*** cmd, char* fileName, int numArgsInCmd[]) {
  // Declare needed variables.
  int fd;			// Used to open file.
  int status;			// Used to wait till child process finishes.  
  int stdin_copy = dup(0);	// Used to copy standard input.
  int stdout_copy = dup(1);	// Used to copy standard output.

  // Fork to get child process.
  pid_t pid; 
  pid = fork();

  // Child process.
  if (pid == 0) {
    // if statement to know whether the file is opened successfully.
    if ((fd = open(fileName, O_CREAT|O_TRUNC|O_WRONLY, 0644)) < 0) {
      perror(fileName);     
      exit(EXIT_FAILURE);
    }      

    // Write to file instead of standard output.
    // Close file descriptor and execute command.
    dup2(fd,STDOUT_FILENO);
    close(fd);
    launch_cmd(cmd[0],numArgsInCmd[0]);
    exit(0);
  } 

  // Parent process.
  // Close file descriptor and wait till child process finishes.
  close(fd);
  wait(&status);

  // Write to standard output again.
  dup2(stdin_copy, 0);
  dup2(stdout_copy, 1);
  close(stdin_copy);
  close(stdout_copy);

  free(fileName);		// Deallocate fileName.

  return 1;
}

/**
 * Method to implement the pipe (|) command.
 * Parameter cmds contains the commands that are going to be executed.
 * Parameter numCmds is the number of commands that need to be connected and executed.
 * Parameter numArgsInCmd is the number of arguments in each command.
 * Format = [cmds] | [cmds]
 * Returns an integer value upon exiting.
 */
int pipeQuash(char*** cmds, int numCmds, int numArgsInCmd[]) {
  // Declare needed variables.
  int i;			// Used in loops.
  pid_t pid;			// Used to create child processes.
  int status;			// Used to wait for child processes.
  int fd[2*(numCmds-1)];	// Used to create pipes. Size depends on numCmds.

  // for-loop to create the pipes.
  for(i = 0; i < numCmds-1; i++) {
    if (pipe(fd + 2*i) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
  }

  // while-loop to execute all sub-commands.
  i = 0;
  while(i < numCmds) {      
    pid = fork();

    // Child process.
    if(pid == 0) {
      // if-else statements to connect the pipes accordingly.
      if(i == 0) {				// First child process.
        dup2(fd[1], STDOUT_FILENO);
      } else if (i == numCmds - 1) {		// Last child process.
        dup2(fd[2*(numCmds-1) - 2], STDIN_FILENO);
      } else {					// Other child process.
        dup2(fd[(i*2)+1], STDOUT_FILENO);
        dup2(fd[(i-1)*2], STDIN_FILENO);
      }

      // for-loop to close all file descriptors.
      int j;
      for(j = 0; j < 2*(numCmds-1); j++) {
        close(fd[j]);
      }
      // Execute command.
      launch_cmd(cmds[i],numArgsInCmd[i]);
      exit(1);
    } 

    i++;	// Continue to next sub-command. 
  }

  // Parent process.
  // for-loop to close all file descriptors.
  for(i = 0; i < 2*(numCmds-1); i++) {
    close(fd[i]);
  }
  // for-loop to wait till all child processes finish.
  for(i = 0; i < numCmds; i++){     
    wait(&status);
  }

  return 1;
}

/**
 * Method to separate the sub-commands in the given command line.
 * Parameter args is the command line that is received from standard input.
 * Returns an integer value upon exiting.
 */
int divideCommand(char** args) {
  // Declare and initialize the needed variables.
  // Initialize the array of commands.
  char*** comms = (char***) malloc (1024 * sizeof(char**));
  if(!comms){
    fprintf(stderr,"divideCommand: allocation error\n");
    exit(EXIT_FAILURE);
  }
  bzero(comms, sizeof(comms));

  // Initialize the array that will hold the number of arguments of each sub-command.
  int numArgsInCmd[1024];	
  int i;
  for (i = 0; i < 1024; i++) {
    numArgsInCmd[i] = 0;
  }

  int numComms = 0;		// Number of sub-commands in a command line.
  int j = 0;    		// Used in loop to signify the current number of sub-command.
  int k = 0;    		// Used in loop to signify the current number of argument.
  int notComsArgs = 0;		// Used to remove symbols ('>','<') from list of arguments
  char* fileNameIn = NULL;	// Used in input redirection.
  char* fileNameOut = NULL;	// Used in output redirection.

  // if-else statement to know whether it is an input or output redirection.
  // Then, remove the symbols and store the filename.
  if((numArgs > 2) && (strcmp(args[numArgs-2], "<") == 0)) {
    notComsArgs += 2;
    fileNameIn = (char*) malloc ((strlen(args[numArgs-1])+1) * sizeof(char));
    strcpy(fileNameIn,args[numArgs-1]);
  } else if((numArgs > 2) && (strcmp(args[numArgs-2], ">") == 0)) {
    notComsArgs += 2;
    fileNameOut = (char*) malloc ((strlen(args[numArgs-1])+1) * sizeof(char));
    strcpy(fileNameOut,args[numArgs-1]);
  } 

  // Initialize the first sub-command.
  comms[0] = (char**) malloc (MAX_COMMAND_LENGTH * sizeof(char**));
  bzero(comms[0], sizeof(comms[0]));

  // while-loop to parse the command line into sub-commands.
  i = 0;
  while(i < numArgs - notComsArgs) {
    if(strcmp(args[i], "|") == 0) {   
      j++;			// Increase the current number of sub-command.
      // Initialize the next sub-command.
      comms[j] = (char**) malloc (MAX_COMMAND_LENGTH * sizeof(*comms[j]));
      bzero(comms[j], sizeof(comms[j]));
      i++;			// Go to the next argument in command line.
      k = 0;			// Reset the current number of argument in sub-command.

    } else {
      // Initialize the argument in sub-command.
      comms[j][k] = (char*) malloc(sizeof(char) * (strlen(args[i])+1));
      strcpy(comms[j][k],args[i]);
      i++;			// Go to the next argument in command line.
      k++;			// Go to the next argument in sub-command.
      numArgsInCmd[j]++;	// Increase the current number of arguments in this sub-command.
    }
  }

  numComms = j + 1;		// Get the number of sub-commands.

  // Set end of sub-commands to NULL. 
  comms[numComms] = NULL;
  for(i = 0; i < numComms; i++) {
    comms[i][numArgsInCmd[i]] = NULL;

  }

  // Call the appropriate functions (pipe, I/O redirection, or executable).
  if(numComms > 1) {
    pipeQuash(comms, numComms, numArgsInCmd);
  } else if((numArgs > 2) && (strcmp(args[numArgs-2], "<") == 0)) {      
    fileIn(comms, fileNameIn, numArgsInCmd); 
  } else if((numArgs > 2) && (strcmp(args[numArgs-2], ">") == 0)) { 
    fileOut(comms, fileNameOut, numArgsInCmd);
  } else {
    launch_cmd(args,numArgs);
  }

  // for-loops to deallocate comms.
  for (i = 0; i < numComms; i++) {		
    for (j = 0; j < numArgsInCmd[i]; j++) {
      free(comms[i][j]);
    }
    free(comms[i]);
  }

  free(comms);

  return 1;
}

/**
 * Method to determine if command is an executable or a built-in function.
 * Parameter args is the command line.
 * Returns an integer value upon exiting.
 */
int execute_cmd(char** args){
  // if statements to check whether there's an input (first argument).
  if(args[0] == NULL){
    return 1;
  }

  // for-loop to check if the first argument matches with any built-in functions.
  int i;
  for(i = 0; i < sizeBuiltIn; i++) {
    if (strcmp(args[0], builtIn[i]) == 0) {
      break;
    }
  }

  // switch statements to redirect the command to the correct implementations.
  // If it's a built-in function, call its function accordingly.
  // Else, execute the command normally.
  switch(i) {

  case 0  :	// set
    return setQuash(args);
    break;
  case 1  :	// echo
    return echoQuash(args);
    break;
  case 2  :	// cd
    return cdQuash(args);
    break;
  case 3  :	// pwd
    return pwdQuash(args);
    break;
  case 4  :	// quit
    terminate();
    break;
  case 5  :	// exit
    terminate();
    break;
  case 6  :	// jobs
    return jobs();
    break;
  case 7  :	// kill
    return killQuash(args);
    break;
  default :	// Executable
    return divideCommand(args);

  }

  return 0;
}

/**
 * Quash entry point
 *
 * @param argc argument count from the command line
 * @param argv argument vector from the command line
 * @return program exit status
 */
int main(int argc, char** argv) { 
  
  command_t cmd; 	//< Command holder argument

  start();		

  // Set up handler to delete job when a background process finishes.
  struct sigaction sa = {.sa_handler = &sigchld_handler};
  sigset_t mask_set;
  sigfillset(&mask_set);
  sa.sa_mask = mask_set;
  sa.sa_flags =  SA_RESTART;
  sigaction(SIGCHLD, &sa, 0);

  puts("Welcome to Quash!");
  puts("Type \"exit\" to quit");

  // Main execution loop
  while (is_running() && get_command(&cmd, stdin)) {
    // NOTE: I would not recommend keeping anything inside the body of
    // this while loop. It is just an example.

    // The commands should be parsed, then executed.
    if (!strcmp(cmd.cmdstr, "exit") || !strcmp(cmd.cmdstr,"quit")) {
      puts("bye");
      terminate(); 			// Exit Quash

    } else {	
      argsa = parse(cmd.cmdstr);	// Get arguments from command line.
      execute_cmd(argsa);		// Execute arguments.

      int i;
      // for-loop to delete arguments once finishes executing.
      for(i = 0; i < numArgs; i++) {
        char* tmp = argsa[i];
        free(tmp);
      }
      free(argsa);
    }  
  }

  return EXIT_SUCCESS;
}
