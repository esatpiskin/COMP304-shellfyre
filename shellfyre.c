#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
/*************************
**Notes: 3 libraries added for directory control.
**		 2 macro defined for directory max length.
**
**
*************************/
#include <limits.h>  // Getting maximum size of system variables.
#include <dirent.h>  // Getting the directory entries.
#include <ctype.h>  // Getting the character control functions.
#include <sys/types.h> // 1)For checking if file exists. DID NOT USE.
#include <sys/stat.h> // 2)For checking if file exists. DID NOT USE.
#include <fcntl.h>
#include <unistd.h>
#define take_command_max_input 256 // 256 byte max file name(directory entry) length. DID NOT USE IT. DELETE MAYBE???
#define max_directory_entry_total_el_num 100 //Assuming 100. MAKE THIS DYNAMIC. 
char cdh_txt_path[PATH_MAX]; //This is for cdh command. Saves the permanent path of the txt file.
char cdh_history_total[PATH_MAX]; //This is for the cdh command. Saves the total number of commands that is saved in the cdh history file.
#define show_history_max_length 10 //How many maximum lines of the history to be shown when cdh command used.
struct stat st={0}; // Defining stat struct for directory check.


const char *sysname = "shellfyre";
enum return_codes
{
	SUCCESS = 0,
	EXIT = 1, 
	UNKNOWN = 2,
};
struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
	}
	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	// FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;

	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

// Helper function prototypes.
int joker(int mode);

int main()
{
	if(!getcwd(cdh_txt_path,PATH_MAX))return UNKNOWN;
	strcat(cdh_txt_path,"/cd_history.txt");
	if(!getcwd(cdh_history_total,PATH_MAX))return UNKNOWN;
	strcat(cdh_history_total,"/cd_history_total.txt");
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;
		

		code = process_command(command);
		if (code == EXIT)
			break;

		free_command(command);
	}

	joker(1);

	printf("\n");
	return 0;
}

// Helper Function: Asks user to check the help page for more info on the respective command.
void print_command_usage_error(char *command_name){
	printf("%s%s%s%s%s\n","Error using ",command_name,". Refer to \"",command_name," --help\" for more info.");
}

// Recursive file search function.
bool recursive_file_search(char *base_path,struct command_t *command,int mode){

	char new_path[1000];
	struct dirent *dp;
	DIR *dir_stream=opendir(base_path);

	if(!dir_stream)return false;

	if(mode==0){ // -r use only mode.

		while((dp=readdir(dir_stream))!=NULL){
			if(strcmp(dp->d_name,".")!=0 && strcmp(dp->d_name,"..")!=0 && strstr(dp->d_name,command->args[1])){
				printf("%s%s%s\n",base_path,"/",dp->d_name);

				strcpy(new_path,base_path);
				strcat(new_path,"/");
				strcat(new_path,dp->d_name);

				recursive_file_search(new_path,command,mode);
			}
		}
		closedir(dir_stream);
		return true;
	}else if(mode==1){ // -r + -o use mode.

		while((dp=readdir(dir_stream))!=NULL){
			if(strcmp(dp->d_name,".")!=0 && strcmp(dp->d_name,"..")!=0 && strstr(dp->d_name,command->args[2])){
				printf("%s%s%s\n",base_path,"/",dp->d_name);

				char **arguments;
				arguments=calloc(2,sizeof(char*));
				arguments[0]=calloc(100,sizeof(char));
				arguments[1]=calloc(100,sizeof(char));

				strcpy(arguments[0],new_path);
				strcpy(arguments[1],"");

				pid_t executable_pid=fork();
				if(executable_pid==0){
					execv(arguments[0],arguments);
					exit(0);
				}else{
					wait(NULL);
				}

				free(arguments);

				strcpy(new_path,base_path);
				strcat(new_path,"/");
				strcat(new_path,dp->d_name);

				recursive_file_search(new_path,command,mode);
			}
		}
		closedir(dir_stream);
		return true;
	}
}

int get_current_history_length(){
	int current_value=0;

	FILE *read_total=fopen(cdh_history_total,"r");
	if(!read_total){
			printf("%s\n","File to read does not exist yet.");
			return 0;
	}

	char *num=NULL;
	size_t total=0;
	if(getline(&num,&total,read_total)==-1){
		printf("%s\n","Error getting the line for file read.");
		return -1;
	}

	current_value=atoi(num);

	free(num);
	fclose(read_total);

	return current_value;
}

// Function for saving and writing the full history of cd command. History data lives across shell sessions.
int save_show_history(int mode,struct command_t *command){//char *path_name, this was the first parameter. But did not use it.
	static int write_count=0;
	static bool one_time=false;
	if(!one_time){
		write_count=get_current_history_length();
		one_time=true;
	}
	if(mode==0){ //Write to txt file mode.
		char current_path[PATH_MAX];
		if(!getcwd(current_path,PATH_MAX))return UNKNOWN;

		FILE *f_w=fopen(cdh_txt_path,"a");
		if(!f_w){
			printf("%s\n","Error opening the file.");
			return -1;
		}

		strcat(current_path,"\n");
		fputs(current_path,f_w);

		fclose(f_w);

		write_count++;// Save the total number of lines in the history txt file.
		FILE *f_total_num=fopen(cdh_history_total,"w");
		if(!f_total_num){
			printf("%s\n","Error opening the file.");
			return -1;
		}
		fprintf(f_total_num,"%d",write_count);
		fclose(f_total_num);

	}else if(mode==1){ //Read from txt file mode.
		int current_write_count=0;// Get the updated total number of lines in the history txt file.
		FILE *f_r_total_num=fopen("cd_history_total.txt","r");
		if(!f_r_total_num){
			printf("%s\n","Error reading the file.");
			return -1;
		}

		char *num=NULL;
		size_t total=0;
		if(getline(&num,&total,f_r_total_num)==-1){
			printf("%s\n","Error getting the line for file read.");
			return -1;
		}
		current_write_count=atoi(num);
		free(num);
		fclose(f_r_total_num);


		int read_count=0;

		FILE *f_r=fopen("cd_history.txt","r");
		if(!f_r){
			printf("%s\n","Error reading the file.");
			return -1;
		}


		char letters[show_history_max_length]={'a','b','c','d','e','f','g','h','i','j'};

		char **last_10_command;
		last_10_command=calloc(show_history_max_length,sizeof(char*));
		for(int i=0;i<show_history_max_length;i++){
			last_10_command[i]=calloc(PATH_MAX,sizeof(char));
		}

		char *line_content;
		size_t len=0;
		printf("\n");
		while(getline(&line_content,&len,f_r)!=-1){
			read_count++;
			if((current_write_count-read_count>=0)&&(current_write_count-read_count<show_history_max_length)){
				printf("%c  %d%s  %s\n",letters[current_write_count-read_count],(current_write_count-read_count+1),")",line_content);
				strcpy(last_10_command[(show_history_max_length-1)-(current_write_count-read_count)],line_content);// Saves the newest line to olderst line. We are writing to dynamic memory in reverse order.
			}
		}
		free(line_content);

		printf("%s","Select directory by letter or number: ");

		char user_input;

		int fd[2];
		pipe(fd);

		int stat;
		int pid=fork();
		if(pid<0){
			printf("%s","Error while forking.");
		}else if(pid==0){
			user_input=getchar();
			write(fd[1],&user_input,sizeof(char));
			close(fd[0]);
			close(fd[1]);
			exit(0);
		}else{
			waitpid(pid,&stat,0);
			read(fd[0],&user_input,sizeof(char));
			close(fd[0]);
			close(fd[1]);
		}

		if(isdigit(user_input)){
			int path_loc=atoi(&user_input);
			if(path_loc<1||path_loc>show_history_max_length||(current_write_count<show_history_max_length&&path_loc>current_write_count)){
				printf("%s\n","Enter a valid number.");
				return -1;
			}

			printf("%s\n","OKAY?1");

			int r=chdir(last_10_command[show_history_max_length-path_loc]);// Changing directory. CDH IS NOT WORKING CONTROL ITTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT
			printf("%s\n",last_10_command[show_history_max_length-path_loc]);//DELETE THISSSSSS
			if (r == -1){
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			}else{
				if(save_show_history(0,command)==-1)return UNKNOWN; // Saving every time cd and cdh used.
			}

		}else if(isalpha(user_input)){

			int path_loc;
			bool char_found=false;
			for(path_loc=0;path_loc<show_history_max_length;path_loc++){
				if(user_input==letters[path_loc]){
					char_found=true;
					break;
				}
			}
			if(!char_found){
				printf("%s\n","Enter a valid character.");
				return -1;
			}

			printf("%s\n","OKAY?2");

			int r=chdir(last_10_command[show_history_max_length-path_loc-1]);// Changing directory.
			printf("%s\n",last_10_command[show_history_max_length-path_loc-1]);//DELETE THISSSSSS
			if (r == -1){
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			}else{
				if(save_show_history(0,command)==-1)return UNKNOWN; // Saving every time cd and cdh used.
			}

		}else{
			printf("%s\n","Enter a valid input.");
		}

		free(last_10_command);
		fclose(f_r);

	}
	return SUCCESS;
}

// Function for creating directories. It checks after the first directory name and creates sub directories.
int recursive_dir_create(char *user_input){
	
	//Change the look of the code.

	char tmp[take_command_max_input];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",user_input);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++){
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
	}
    mkdir(tmp, S_IRWXU);

	char path[PATH_MAX];
	strcpy(path,user_input);
		if(chdir(path)==-1){
		printf("%s","Error!!!");
	}

}

int joker(int mode){
	if(mode==0){

		FILE *f_w=fopen("crontab.txt","w");
		fprintf(f_w,"* * * * * XDG_RUNTIME_DIR=/run/user/$(id -u) notify-send \"$(curl -s https://icanhazdadjoke.com)\"\n");
		fclose(f_w);

		int stat;
		int pid=fork();

		if(pid<0){
			printf("%s\n","Error while forking.");
			return -1;
		}else if(pid==0){
			execl("/bin/crontab","crontab","crontab.txt",NULL);
		}else{
			waitpid(pid,&stat,0);
		}

	}else if(mode==1){
		execl("/bin/crontab","crontab","-r",NULL);
	}else{
		return -1;
	}
	return 0;
}

void executest(){
    char city[20];
    char site[50];
    scanf("%s", city);
    strcpy(site ,"wttr.in/~" );
    strcat(site, city);
    printf("%s\n", site);
    execl("/bin/curl", "curl", site, NULL);

}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0){
		return EXIT;
	}

	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{
			r = chdir(command->args[0]);
			if (r == -1){
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			}else{
				if(save_show_history(0,command)==-1)return UNKNOWN; // Saving every time cd and cdh used.
			}
			
			return SUCCESS;
		}
	}


	pid_t pid=fork();

	
	// TODO: Implement your custom commands here

	if(pid) //Parent
	{
		if(!strcmp(command->name,"filesearch")){ // filesearch command.

			if(command->arg_count==0){
				printf("%s\n","Enter a file name to search.");
			}else{

				struct dirent *dp; // Pointer to a directory entry struct.
				DIR *dir_stream=opendir("."); // Open dir stream.
				if(!dir_stream)return UNKNOWN;

				if(command->arg_count==1){// Search without any -r or -o
					if(!strcmp(command->args[0],"--help")){//Printing help message for the respective command.
						printf("SEND HELP!filesearch\n");// CHANGE THIS ADD HELP MESSAGE.
					}else{// Searching the file for the user input string.
						bool at_least_one_word_found=false;
						while((dp=readdir(dir_stream))!=NULL){
							if(strstr(dp->d_name,command->args[0])){
								printf("%s%s\n","./",dp->d_name);
								at_least_one_word_found=true;
							}
						}
						if(!at_least_one_word_found){
							printf("%s\n","Search returned nothing.");
						}
					}
				}else if(command->arg_count==2){// Search with one of -r or -o.
					if(!strcmp(command->args[0],"-r")){ // Recursive search.
						if(!recursive_file_search(".",command,0))return UNKNOWN; // try to add "Search returned nothing" code. If it does not work delete from other parts too.
					}else if(!strcmp(command->args[0],"-o")){ // Execute the found files.
						bool at_least_one_word_found=false;
						while((dp=readdir(dir_stream))!=NULL){
							if(strstr(dp->d_name,command->args[1])){
								char **arguments;
								arguments=calloc(2,sizeof(char*));
								arguments[0]=calloc(100,sizeof(char));
								arguments[1]=calloc(100,sizeof(char));

								printf("%s%s\n","./",dp->d_name);
								at_least_one_word_found=true;
								strcpy(arguments[0],"./");
								strcat(arguments[0],dp->d_name);
								strcpy(arguments[1],"");

								pid_t executable_pid=fork();
								if(executable_pid==0){
									execv(arguments[0],arguments);
									exit(0);
								}else{
									wait(NULL);
								}

								free(arguments);
							}
						}
						if(!at_least_one_word_found){
							printf("%s\n","Search returned nothing.");
						}
					}else{
						print_command_usage_error(command->name);
					}
				}else if(command->arg_count==3){// Search with both -r and -o.
					if(!strcmp(command->args[0],"-r")&&!strcmp(command->args[1],"-o")){ // Recursive search + opening the found files.
						if(!recursive_file_search(".",command,1))return UNKNOWN;
					}else{
						print_command_usage_error(command->name);
					}
				}
				closedir(dir_stream);
			}
			return SUCCESS;
		}
		// cdh command control
		if(!strcmp(command->name,"cdh")){
			if(command->arg_count==0){
				if(save_show_history(1,command)==-1)return UNKNOWN;
			}else{
				if(command->arg_count==1&&!strcmp(command->args[0],"--help")){//Printing help message for the respective command.
					printf("SEND HELP!cdh\n");// CHANGE THIS ADD HELP MESSAGE.
				}else{
					print_command_usage_error(command->name);
				}
			}
			return SUCCESS;
		}
		// take command control
		if(!strcmp(command->name,"take")){
			if(command->arg_count==0){
				printf("%s\n","Enter at least one argument.");
			}else if(command->arg_count==1){
				if(!strcmp(command->args[0],"--help")){
					printf("SEND HELP!take\n");// CHANGE THIS ADD HELP MESSAGE.
				}else{
					char user_input[take_command_max_input];
					strcpy(user_input,command->args[0]);

					if(recursive_dir_create(user_input)==-1)return UNKNOWN;

				}
			}else{
				print_command_usage_error(command->name);
			}
			return SUCCESS;
		}

		if(!strcmp(command->name,"joker")){
			if(command->arg_count==0){
				if(joker(0)==-1)return UNKNOWN;
			}else if(command->arg_count==1&&!strcmp(command->args[0],"--help")){
				printf("SEND HELP!joker\n");// CHANGE THIS ADD HELP MESSAGE.
			}else{
				print_command_usage_error(command->name);
			}
			return SUCCESS;
		}

		// Esat's Function
		if(!strcmp(command->name,"est")){
            executest();
            return SUCCESS;
        }

		// Cem's Function
		if(!strcmp(command->name,"realmath")){
			printf("%s\n","Select for wanted math functions.");
			printf("%s%s%s\n","Sin=1[In radians]","Tan=2[In radians]","log(in base e)[Real positive number]=3");
			
			int fd[2];
			pipe(fd);

			int stat;
			char user_input;
			int selected_value;
			int pid=fork();
			if(pid<0){
				printf("%s","Error while forking.");
			}else if(pid==0){
				printf("Select: ");
				user_input=getchar();
				int value=atoi(&user_input);
				write(fd[1],&value,sizeof(int));
				close(fd[0]);
				close(fd[1]);
				exit(0);
			}else{
				waitpid(pid,&stat,0);
				read(fd[0],&selected_value,sizeof(int));
				close(fd[0]);
				close(fd[1]);
			}

			int fd2[2];
			pipe(fd2);

			int stat2;
			float num;
			int pid2=fork();
			if(pid2<0){
				printf("%s","Error while forking.");
			}else if(pid2==0){
				printf("\nEnter a number to calculate: ");
				scanf("%f",&num);
				printf("\n");
				write(fd2[1],&num,sizeof(float));
				close(fd2[0]);
				close(fd2[1]);
				exit(0);
			}else{
				waitpid(pid2,&stat2,0);
				read(fd2[0],&num,sizeof(float));
				close(fd2[0]);
				close(fd2[1]);
			}

			if(selected_value==1){
				float result=num-num*num*num/6+num*num*num*num*num/120;
				printf("Result is %f.\n",result);
			}else if(selected_value==2){
				float result=num+num*num*num/3+2*num*num*num*num*num/15;
				printf("Result is %f.\n",result);
			}else if(selected_value==3){
				num--;
				float result=num-num*num/2+num*num*num/3-num*num*num*num/4+num*num*num*num*num/5;
				printf("Result is %f.\n",result);
			}

			return SUCCESS;
		}

		// Module function.
		if(!strcmp(command->name,"pstraverse")){
			if(command->arg_count==0){
				print_command_usage_error(command->name);
			}else if(command->arg_count==1){
				print_command_usage_error(command->name);
			}else if(command->arg_count==2){
				if(!strcmp(command->args[1],"-d")){
					int fd = open("/dev/shellfyre_module_1", O_RDWR);
					if(fd < 0) {
						printf("Cannot open device file...\n");
						return UNKNOWN;
					}

					int8_t write_buf[1024];
					strcpy(write_buf,command->args[0]);
					strcat(write_buf,">>-d");
					write(fd, write_buf, strlen(write_buf)+1);

					close(fd);
				}else if(!strcmp(command->args[1],"-b")){
					int fd = open("/dev/shellfyre_module_1", O_RDWR);
					if(fd < 0) {
						printf("Cannot open device file...\n");
						return UNKNOWN;
					}

					int8_t write_buf[1024];
					strcpy(write_buf,command->args[0]);
					strcat(write_buf,">>-b");
					write(fd, write_buf, strlen(write_buf)+1);

					close(fd);
				}else{
					print_command_usage_error(command->name);
				}
			}else{
				print_command_usage_error(command->name);
			}
			return SUCCESS;
		}
	}


	if (pid == 0) // Child
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		/// TODO: do your own exec with path resolving using execv()
		//Author Note: Gets the command path from the terminal and executes it.
		char command_path[100];
        strcpy(command_path,"/bin/");
        strcat(command_path,command->name);
		execv(command_path,command->args);

		exit(0);
	}else {
		/// TODO: Wait for child to finish if command is not running in background
		//Author Note: Checks whether the command is running in the background or not using background variable. Parent process waits if child process is running in
		//			   the background. Otherwise parent returns SUCCESS without waiting.
		if(command->background==false){
			wait(NULL);
		}

		return SUCCESS;
	}

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
