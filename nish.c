#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>

typedef struct job {
    int* pid;
    int background;
    int pid_idx;
    int job_id;
    int num_progs;
    int* arg_num;
    char*** arg_list;
    int can_be_removed;
}job_t;

char* cwd;
job_t* foreground_job;
job_t* job_array[32];
pid_t shell_pgid;
int num_jobs = 0;
int shell_terminal;
int batch_mode = 0;

void job_deconstructor(job_t* ptr){
    //check if the job is null
    if(ptr == NULL){
        return;
    }
    //go through all of the malloc'ed parts of the struct, and
    //free in order
    if (ptr->pid != NULL) {
        free(ptr->pid);
        ptr->pid = NULL;
    }
    if (ptr->arg_list != NULL) {
        for (int i = 0; i < ptr->num_progs; i++) {
            if (ptr->arg_list[i] != NULL) {
                for (int j = 0; j < ptr->arg_num[i]; j++) {
                    if (ptr->arg_list[i][j]!=NULL){
                        free(ptr->arg_list[i][j]);
                        ptr->arg_list[i][j] = NULL;
                    }
                }
                free(ptr->arg_list[i]);
                ptr->arg_list[i] = NULL;
            }
        }
        free(ptr->arg_list);
        ptr->arg_list = NULL;
    }
    if (ptr->arg_num != NULL) {
        free(ptr->arg_num);
    }
}

void handler_SIGINT(int signum){
    //filled in because technically, handlers take in signum
    //and compiler gets mad about empty function
    signum = signum;
}

void insert_job(job_t* job_to_insert){
    //search through the job array and if the current job cannot be removed and
    //isn't the same job as the one we are looking to insert, then let us check
    //if it has terminated
    for (int i = 0; i < num_jobs; i++){
        int all_finished = 1;
        if (!job_array[i]->can_be_removed || job_to_insert != job_array[i]){
            for (int proc_id = 0; proc_id < job_array[i]->num_progs; proc_id++){
                int stat = -10;
                int wait_result = waitpid(job_array[i]->pid[proc_id], &stat, WNOHANG);
                if (wait_result==0){
                    all_finished = 0;
                }
            }
        }
        //if all processes in job have finished or it can be removed, or it is
        //the same as the current job, let us put our job_to_insert in that
        //position
        if (all_finished){
            int id_cpy = job_array[i]->job_id;
            //if the job is actually finished, as in terminated/exited we can
            //feel free to destroy its struct and put our new struct in there,
            // feels like we recreated OOP with this one
            if (!job_array[i]->can_be_removed || job_to_insert != job_array[i]){
                job_deconstructor(job_array[i]);
                free(job_array[i]);
                job_array[i] = NULL;
            }
            job_array[i] = job_to_insert;
            job_to_insert->job_id = id_cpy;
            return;
        }
    }
    //otherwise if we can't find an empty space to put our job, let us give up
    //and put it in the next free spot outside the space occupied by previous
    //jobs
    job_array[num_jobs] = job_to_insert;
    job_to_insert->job_id = num_jobs+1;
    num_jobs += 1;
}

int find_job(int job_id, int max_job){
    //if we are just looking for the max job, let us find the job with the
    //highest job idx that cannot be removed
    if(max_job){
        int max_job_id = -1;
        int max_job_idx = -1;
        for(int i = 0; i < num_jobs; i++){
            if (max_job_id < job_array[i]->job_id && !job_array[i]->can_be_removed){
                max_job_id = job_array[i]->job_id;
                max_job_idx = i;
            }
        }
        return max_job_idx;
    }
    //if not looking for max job, go for a straight job_id match
    for (int i = 0; i < num_jobs; i++){
        if (job_array[i]->job_id==job_id && !job_array[i]->can_be_removed){
            return i;
        }
    }
    return -1;
}

int find_fgjob(int job_id, int max_job){
    //functionally the same as find_job above, except the job it returns must be
    //a foreground job
    if(max_job){
        int max_job_id = -1;
        int max_job_idx = -1;
        for(int i = 0; i < num_jobs; i++){
            if (max_job_id < job_array[i]->job_id && !job_array[i]->background){
                max_job_id = job_array[i]->job_id;
                max_job_idx = i;
            }
        }
        return max_job_idx;
    }
    for (int i = 0; i < num_jobs; i++){
        if (job_array[i]->job_id==job_id && !job_array[i]->background){
            return i;
        }
    }
    return -1;
}

//simple do while loop that checks if the process has finished running/has
//stopped
void wait_for_job (pid_t pid, int* status){
    do{
        pid_t wpid = waitpid(pid, status, WUNTRACED | WCONTINUED);
        if (wpid == -1){
            dprintf(2, "%i",pid);
            perror("waitpid, wait_for_job");
            exit(-1);
        }
    } while (!WIFSTOPPED(*status) && !WIFEXITED(*status) && !WIFSIGNALED(*status));
}

void send_job_background(job_t* fg_job){
    //if not in batch mode, let us give terminal control back to nish
    if(!batch_mode){
        tcsetpgrp(STDIN_FILENO, getpid());
    }
    //take the foreground job, put in our jobs list, set the foreground job ptr
    //to null and finally make sure that we set the fg_job to not being
    //removable
    insert_job(fg_job);
    if (foreground_job == fg_job){
        foreground_job = NULL;
    }
    fg_job->can_be_removed = 0;
}

void send_job_foreground(job_t* bg_job){
    //grab the pgid and make the job removable since it is now in the foreground
    pid_t new_pgid = bg_job->pid[0];
    bg_job->can_be_removed = 1;
    foreground_job = bg_job;
    //wake up every job in the process group
    for(int i = 0; i < bg_job->pid_idx;i++){
        kill(bg_job->pid[i], SIGCONT);
    }
    //give process group terminal control and let us wait for it,
    //after we are done waiting, check if it was stopped, if so,
    //we must put it back in our jobs list
    for(int i = 0; i < bg_job->pid_idx; i++){
        int status = -1;
        if(!batch_mode)
            tcsetpgrp(STDIN_FILENO,new_pgid);
        wait_for_job(bg_job->pid[i], &status);
        if(!batch_mode)
            tcsetpgrp(STDIN_FILENO,getpid());
        if(WIFSTOPPED(status)){
            send_job_background(bg_job);
            bg_job->can_be_removed = 1;
            killpg(bg_job->pid[0], SIGTSTP);
        } else if (WIFEXITED(status)){
            if (WEXITSTATUS(status)==127){
                printf("Command %s not found!\n", bg_job->arg_list[i][0]);
            }
        }
    }
    bg_job->can_be_removed = 0;
}

void handler_SIGTSTP(int signum){
    signum = signum;
}

//takes the current line passed in by the user, and splits it along the pipe
//character into discrete strings which are returned in the programs argument
// the return struct will have to freed once done with
int split_line_to_programs(char** programs, char* curr_line){
    int curr_idx = 0;
    char* curr_prog;
    if (programs==NULL){
        printf("Error allocating the progs buffer\n");
    }
    char* curr_line_cp = strdup(curr_line);
    if (curr_line_cp == NULL){
        perror("strdup in split_line_to_programs");
    }
    curr_prog = strtok(curr_line_cp,"|");
    while (curr_prog != NULL){
        programs[curr_idx] = strdup(curr_prog);
        curr_idx += 1;
        curr_prog = strtok(NULL, "|");
    }
    programs[curr_idx] = NULL;
    free(curr_line_cp);
    return curr_idx;
}

//splits each process command (not pipe) into words, fuctionally returning what
//C passes in the argv array
int split_line(char** args, char* curr_line){
    int curr_idx = 0;
    char* curr_arg;
    if (args==NULL){
        printf("Error allocating the args buffer\n");
    }
    curr_arg = strtok(curr_line," \t\r\n\a");
    while (curr_arg != NULL){
        args[curr_idx] = strdup(curr_arg);
        curr_idx += 1;
        curr_arg = strtok(NULL, " \t\r\n\a");
    }
    args[curr_idx] = NULL;
    return curr_idx;
}

char* format_job(job_t* job) {
    int i, j;
    char* formatted_str = NULL;
    char* ampersand = "";

    //check if the job is a background job
    if (job->background) {
        ampersand = "&";
    }

    //Calculate the total length needed for the formatted string, I should
    // use Coq and formal methods verification to prove soundness
    int total_length = 0;
    total_length += snprintf(NULL,
                       0,
                       "%d: ",
                       job->job_id);
    for (i = 0; i < job->num_progs; i++) {
        for (j = 0; j < job->arg_num[i]; j++) {
            total_length += snprintf(NULL, 0, "%s ", job->arg_list[i][j]);
        }
        if (i < job->num_progs - 1) {
            total_length += snprintf(NULL, 0, "| ");
        }
    }
    total_length += snprintf(NULL,0, "%s", ampersand);
    //Allocate memory for the formatted string
    formatted_str = (char*)malloc(total_length + 1);
    if (formatted_str == NULL) {
        return NULL; // Memory allocation failed
    }

    //Construct the formatted string
    int offset = 0;
    offset += snprintf(formatted_str,
                       total_length - offset,
                       "%d: ",
                       job->job_id);
    for (i = 0; i < job->num_progs; i++) {
        for (j = 0; j < job->arg_num[i]; j++) {
            offset += snprintf(formatted_str + offset, total_length - offset, "%s ", job->arg_list[i][j]);
        }
        if (i < job->num_progs - 1) {
            offset += snprintf(formatted_str + offset, total_length - offset, "| ");
        }
    }
    snprintf(formatted_str + offset, total_length - offset + 1, "%s", ampersand);
    return formatted_str;
}

//move the job to foreground, by either finding the max job_id
// or by the argument passed in
void fg(char** args, int arg_count){
    if(arg_count==1){
        int job_loc = find_job(0,1);
        if (job_loc == -1){
            printf("Job not found!\n");
        }
        if (job_loc >= 0){
            send_job_foreground(job_array[job_loc]);
        }
    }
    if (arg_count==2){
        int arg_val = atoi(args[1]);
        if(arg_val==0){
            printf("Job value not understood!\n");
        }
        int job_loc = find_job(arg_val,0);
        if (job_loc == -1){
            printf("Job not found!\n");
        }
        if (job_loc >= 0){
            send_job_foreground(job_array[job_loc]);
        }
    }
}
//moves job to the background by using the send_job_background fucntion and
//telling the process to continue
void bg(char** args, int arg_count){
    if (arg_count==1){
        int job_loc = find_fgjob(0,1);
        if (job_loc == -1){
            printf("Job not found!\n");
        }
        if (job_loc >= 0){
            job_t *tmp_ptr = job_array[job_loc];
            tmp_ptr -> can_be_removed = 1;
            send_job_background(tmp_ptr);
            tmp_ptr -> can_be_removed = 0;
            killpg(tmp_ptr->pid[0], SIGCONT);
        }
    }
    if (arg_count==2){
        int arg_val = atoi(args[1]);
        if(arg_val==0){
            printf("Job value not understood!\n");
        }
        int job_loc = find_fgjob(arg_val,0);
        if (job_loc == -1){
            printf("Job value not found!\n");
        }
        if (job_loc >= 0){
            job_t *tmp_ptr = job_array[job_loc];
            tmp_ptr -> can_be_removed = 1;
            send_job_background(tmp_ptr);
            tmp_ptr -> can_be_removed = 0;
            killpg(tmp_ptr->pid[0], SIGCONT);
        }
    }
}

//wrapper function to print out all the jobs
void print_jobs(int out_fd){
    for (int i = 0; i < num_jobs; i++){
        int any_running = 0;
        for (int proc_id = 0; proc_id < job_array[i]->num_progs; proc_id++){
            int stat = -10;
            int wait_result = waitpid(job_array[i]->pid[proc_id], &stat, WNOHANG);
            if (wait_result==0){
                any_running= 1;
                break;
            }
        }
        if (any_running && !job_array[i]->can_be_removed){
            char* to_print = format_job(job_array[i]);
            dprintf(out_fd,"%s\n", to_print);
            free(to_print);
        }
    }
}
//function which when give a job struct, forks and execs all the processes in
//the job, and sets their pipe file descriptors
int run_command(char **args,
                job_t* curr_job,
                int input_fd,
                int output_fd,
                pid_t pgid){
    pid_t pid;
    //create the process for the child
    pid = fork();
    if (pid == 0){
        setpgid(0, pgid);

        //setup the child's input and output streams
        if (input_fd != 0){
            dup2(input_fd,0);
            close(input_fd);
        }
        if (output_fd != 1){
            dup2(output_fd, 1);
            close(output_fd);
        }
        //I think this is funny
        if(execvp(args[0], args)==-1){
            int err = errno;
            if (err==2){
                exit(127);
            }
            exit(EXIT_FAILURE);
        }
        //should be unreachable if execvp succeeds
        exit(-1);
    } else if (pid<0){
        perror("Forking failed, fork this!\n");
        exit(-1);
    } else{
        //add pid into the struct
        curr_job->pid[curr_job->pid_idx] = pid;
        curr_job->pid_idx += 1;
    }
    return pid;
}

void print_history(char* path_str, int out_fd){
    struct stat buf;
    if (!stat(path_str, &buf)){
        FILE *fp = fopen(path_str, "r");
        if (fp != NULL) {
            int total_line_count = 0;
            int nread = 0;
            size_t len = 0;
            char* line = NULL;
            while ((nread = getline(&line, &len, fp)) != -1) {
                total_line_count += 1;
                dprintf(out_fd,"%i: %s", total_line_count, line);
            }
            dprintf(out_fd,"Total Lines Read: %i\n",total_line_count);
            fclose(fp);
            return;
        }else{
            printf("cannot open file\n");
        }
    }
}

int main(int argc, char* argv[]) {
    //char* curr_line = NULL;
    foreground_job = NULL;
    cwd = (char*)malloc(256*sizeof(char));
    //size_t len = 0;
    ssize_t nread;
    int buffer_size = 32;
    int batch_mode = 0;
    //check if we are in batch mode
    if (argc == 2){
        struct stat buf;
        if (!stat(argv[1], &buf)){
            FILE *fp;
            fp = freopen(argv[1], "r", stdin);
            if (fp == NULL){
                perror("freopen returned NULL");
                exit(-1);
            }
            batch_mode = 1;
        } else{
            perror("batch file not found");
            exit(-1);
        }
    }
    struct stat buf;
    if (stat(".nishistory",&buf)==-1){
        printf("Opening history!\n");
        FILE* history_fp = fopen(".nishistory" ,"w");
        printf("%p\n",(void*)history_fp);
        fclose(history_fp);
    }

    struct sigaction sig_ttou;
    memset(&sig_ttou,0,sizeof(sig_ttou));
    sig_ttou.sa_handler = SIG_IGN;
    if (sigaction(SIGTTOU,&sig_ttou,NULL)!=0){
        printf("Error binding SIGINT\n");
        exit(-1);
    }
    //block a bunch of signals that we don't need/want, used sigaction as is
    //portable
    struct sigaction sig_int;
    memset(&sig_int,0,sizeof(sig_int));
    sig_int.sa_handler = handler_SIGINT;
    sig_int.sa_flags = SA_RESTART;
    if (sigaction(SIGINT,&sig_int,NULL)!=0){
        printf("Error binding SIGINT\n");
        exit(-1);
    }

    struct sigaction sig_tstp;
    memset(&sig_tstp,0,sizeof(sig_tstp));
    sig_tstp.sa_handler = handler_SIGTSTP;
    sig_tstp.sa_flags = SA_RESTART;
    if (sigaction(SIGTSTP,&sig_tstp,NULL)!=0){
        printf("Error binding SIGTSTP\n");
        exit(-1);
    }
    //make sure our shell is in its own process group,
    //and we shellfishly (pun intended) take terminal control
    //I believe we should be kindly requesting it by waiting till we are in the
    //foreground process group then assuming control, but I think it is fine to
    //arbitrarily seize the foreground
    shell_pgid = getpid();
    setpgid(shell_pgid,shell_pgid);
    if(!batch_mode){
        if (tcsetpgrp(STDIN_FILENO, shell_pgid) == -1) {
            perror("tcsetpgrp");
            printf("tcset failure");
            exit(-1);
        }
    }
    //main shell loop
    using_history();
    read_history(".nishistory");
    if (getcwd(cwd, 256) == NULL) {
        perror("getcwd() error");
        exit(EXIT_FAILURE);
    }
    char prompt[259];
    while (1){
        //malloc space for our programs for the split_line_to_programs function
        char** programs = malloc(buffer_size * sizeof(char*));
        if(programs==NULL){
            exit(-1);
        }
        snprintf(prompt, sizeof prompt, "nish %s>",cwd);
        //get currentline and check if EOF
        char* curr_line = readline(prompt);
        nread = 0;
        if (curr_line != NULL){
            nread = strlen(curr_line);
        }else {
            write_history(".nishistory");
            exit(0);
        }
        add_history(curr_line);
        //clean up whitespace and the such
        while (nread > 0 && (curr_line[nread - 1] == ' ' || curr_line[nread - 1] == '\t' || curr_line[nread - 1]== '\n')) {
            curr_line[nread - 1] = '\0';
            nread--;
        }

        // Check if the last non-whitespace character is '&'
        int is_background = 0;
        if (nread > 0 && curr_line[nread - 1] == '&') {
            is_background = 1;
            // Remove the '&' character from the line
            curr_line[nread - 1] = '\0';
        }
        //create a job array for the user next input
        job_t* curr_job = malloc(sizeof(job_t));
        if(curr_job==NULL){
            exit(-1);
        }
        int num_programs = split_line_to_programs(programs, curr_line);
        //seems like a constructor function would be a really fun and important
        //addition
        curr_job->can_be_removed=0;
        curr_job->background = is_background;
        curr_job->num_progs = num_programs;
        curr_job->pid_idx = 0;
        curr_job->pid = malloc(sizeof(int)*num_programs);
        if(curr_job->pid==NULL){
            exit(-1);
        }
        curr_job->arg_num = malloc(sizeof(int)*num_programs);
        if(curr_job->arg_num==NULL){
            exit(-1);
        }
        curr_job->arg_list = malloc(sizeof(char**)*num_programs);
        if(curr_job->arg_list==NULL){
            exit(-1);
        }
        //create a pipe and pgid variables for pipes
        int first_real_process = 1;
        int input_fd = 0;
        pid_t gpid = 0;
        int pipe_fds[2];
        //for as many processes as the user puts, let us fork/exec and connect
        //the fd's for the pipes (mostly handled by run_command)
        for(int idx = 0; idx < num_programs;idx++){
            //create 2d array for the arguments for the current process in the
            //job
            char** args = malloc(buffer_size * sizeof(char*));
            if(args==NULL){
                exit(-1);
            }
            int num_args = split_line(args,programs[idx]);
            //after turning the current process' line into discrete words we can
            //free that string from the programs array
            free(programs[idx]);
            programs[idx]= NULL;
            //set the arg_list and num_args in the jobs struct
            curr_job->arg_num[idx]=num_args;
            curr_job->arg_list[idx]=args;
            //if the user provides a real command, check if built-in
            if (num_args >= 1){
                if (idx < num_programs - 1){
                    if(pipe(pipe_fds)== -1){
                        perror("failure creating pipe");
                        exit(-1);
                    }
                } else {
                    pipe_fds[1] = 1;
                }
                if(strncmp(args[0],"exit",4) == 0){
                    //clean up code
                    free(programs);
                    programs=NULL;
                    job_deconstructor(curr_job);
                    free(curr_job);
                    curr_job = NULL;
                    for(int i = 0; i < num_jobs; i++){
                        job_deconstructor(job_array[i]);
                        free(job_array[i]);
                        job_array[i] = NULL;
                    }
                    free(cwd);
                    write_history(".nishistory");
                    exit(0);
                }else if (strncmp(args[0], "history",7)==0){
                    write_history(".nishistory");
                    print_history(".nishistory",pipe_fds[1]);
                } else if(strncmp(args[0],"cd",2) == 0){
                    if(num_args!=2){
                        printf("cd requires a single argument\n");
                    }else{
                        if(chdir(args[1]) != 0){
                            printf("Failed to open {%s}\n",args[1]);
                        }else{
                            getcwd(cwd, 256);
                        }
                    }
                } else if (strncmp(args[0],"jobs",4)==0){
                    if(num_args!=1){
                        printf("jobs takes in no arguments\n");
                    }
                    print_jobs(pipe_fds[1]);
                } else if (strncmp(args[0],"fg",2)==0){
                    if(num_args!=1 && num_args != 2){
                        printf("fg takes in either one or no arguments\n");
                    }
                    fg(args,num_args);
                } else if (strncmp(args[0],"bg",2)==0){
                    if(num_args!=1 && num_args != 2){
                        printf("bg takes in either one or no arguments\n");
                    }
                    bg(args,num_args);
                } else{
                    //get the pid of the process just cfrreated
                    pid_t temp_pid = run_command(args, curr_job, input_fd, pipe_fds[1], gpid);
                    //if the write end of the pipe isn't stdout, close it

                    //grab pid of the first process in job, this is our pgid now
                    if (first_real_process){
                        gpid = temp_pid;
                        first_real_process = 0;
                    }
                }
                if (pipe_fds[1] != 1){
                    close(pipe_fds[1]);
                }
                //set our input fd as the read end of the pipe created in
                //the previous iteration, like reversing a linked list
                input_fd = pipe_fds[0];
            }
        }
        //send the job to the foreground or background
        if(!is_background){
            send_job_foreground(curr_job);
        } else {
            send_job_background(curr_job);
        }
        //free up that program line we alloced
        free(curr_line);
        free(programs);
    }
    exit(0);
}
