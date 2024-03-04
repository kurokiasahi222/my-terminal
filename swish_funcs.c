#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    // TODO Task 0: Tokenize string s
    // Assume each token is separated by a single space (" ")
    // Use the strtok() function to accomplish this
    // Add each token to the 'tokens' parameter (a string vector)
    // Return 0 on success, -1 on error
    // char *s will be the cmd
    char *word = strtok(s, " ");
    while (word != NULL) {
        // printf("%s\n", word);
        int ret = strvec_add(tokens, word);  // strvec_add will return -1 if it fails
        if (ret == -1) {
            fprintf(stderr, "strvec_add failed"); 
            return -1;
        }
        word = strtok(NULL, " ");
    }  
    return 0;
}

int run_command(strvec_t *tokens) {
    // TODO Task 2: Execute the specified program (token 0) with the
    // specified command-line arguments
    // THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN SHELL PROCESS
    // Hint: Build a string array from the 'tokens' vector and pass this into execvp()
    // Another Hint: You have a guarantee of the longest possible needed array, so you
    // won't have to use malloc.
    // create an array of char *'s
    char *args[tokens->length];
    for (int i = 0; i < tokens->length; i++) {
        args[i] = strvec_get(tokens, i);
    }
    // add the NULL terminator
    args[tokens->length] = NULL;

    // TODO Task 3: Extend this function to perform output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    // Use dup2() to redirect stdin (<), stdout (> or >>)
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL

    // check for > (redirect output)
    int index = strvec_find(tokens, ">");
    if (index != -1) {
        // open the file for writing
        int fd = open(strvec_get(tokens, index + 1), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            perror("Failed to open input file");
            return -1;
        }
        // redirect stdout
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            return -1;
        }
        // close the file
        if (close(fd) == -1) {
            perror("close");
            return -1;
        }
        // remove the > and the file name from the args array
        for (int i = index; i < tokens->length - 2; i++) {
            args[i] = args[i + 2];
        }
        args[tokens->length - 2] = NULL;
        args[tokens->length - 1] = NULL;
    }

    // check for >> (redirect and append output)
    index = strvec_find(tokens, ">>");
    if (index != -1) {
        // open the file for appending
        int fd = open(strvec_get(tokens, index + 1), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            perror("Failed to open input file");
            return -1;
        }
        // redirect stdout
        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            return -1;
        }
        // close the file
        if (close(fd) == -1) {
            perror("close");
            return -1;
        }
        // remove the >> and the file name from the args array
        for (int i = index; i < tokens->length - 2; i++) {
            args[i] = args[i + 2];
        }
        args[tokens->length - 2] = NULL;
        args[tokens->length - 1] = NULL;
    }
    // check for < (redirect input)
    index = strvec_find(tokens, "<");
    if (index != -1) {
        // open the file for reading
        int fd = open(strvec_get(tokens, index + 1), O_RDONLY);
        if (fd == -1) {
            perror("Failed to open input file");
            return -1;
        }
        // redirect stdin
        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2");
            return -1;
        }
        // close the file
        if (close(fd) == -1) {
            perror("close");
            return -1;
        }
        // remove the < and the file name from the args array
        for (int i = index; i < tokens->length - 2; i++) {
            args[i] = args[i + 2];
        }
        args[tokens->length - 2] = NULL;
        args[tokens->length - 1] = NULL;
    }

    // TODO Task 4: You need to do two items of setup before exec()'ing
    // 1. Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    // 2. Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID
    struct sigaction sac;
    sac.sa_handler = SIG_DFL;
    if (sigfillset(&sac.sa_mask) == -1) {
        perror("sigfillset");
        return 1;
    }
    sac.sa_flags = 0;
    if (sigaction(SIGTTIN, &sac, NULL) == -1 || sigaction(SIGTTOU, &sac, NULL) == -1) {
        perror("sigaction");
        return 1;
    }
    if (setpgid(getpid(), getpid()) == -1) {
        perror("setpgid");
        return 1;
    }

    // exec the program
    if (execvp(args[0], args) == -1) {
        perror("exec");
        return -1;
    }
    // Not reachable after a successful exec(), but retain here to keep compiler happy
    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    // TODO Task 5: Implement the ability to resume stopped jobs in the foreground
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    //    Feel free to use sscanf() or atoi() to convert this string to an int
    // 2. Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    // 3. Send the process the SIGCONT signal with the kill() system call
    // 4. Use the same waitpid() logic as in main -- dont' forget WUNTRACED
    // 5. If the job has terminated (not stopped), remove it from the 'jobs' list
    // 6. Call tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
    //    process's pid, since we call this function from the main shell process

    // foreground
    if (is_foreground) {
        // 1. 
        char *index = strvec_get(tokens, 1); 
        int job_index = atoi(index);
        if (job_index >= jobs->length) {
            fprintf(stderr, "Job index out of bounds\n"); 
            return -1;
        }
        job_t *job = job_list_get(jobs, job_index);
        if (job == NULL) {
            fprintf(stderr, "Job not found"); 
            return -1;
        }
        // 2. 
        if (tcsetpgrp(STDIN_FILENO, job->pid) == -1) {
            perror("tcsetpgrp");
            return -1;
        }
        // 3.
        if (kill(job->pid, SIGCONT) == -1) {
            perror("kill");
            return -1;
        }
        // 4. wait for the problem to terminate or be paused once again
        int status;
        if (waitpid(job->pid, &status, WUNTRACED) == -1) {
            perror("waitpid");
            return -1;
        }   
        // 5. remove from job list
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job_list_remove(jobs, job_index);
        }
        // 6. 
        if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
            perror("tcsetpgrp");
            return -1;
        }
    } else {
        // TODO Task 6: Implement the ability to resume stopped jobs in the background.
        // This really just means omitting some of the steps used to resume a job in the foreground:
        // 1. DO NOT call tcsetpgrp() to manipulate foreground/background terminal process group
        // 2. DO NOT call waitpid() to wait on the job
        // 3. Make sure to modify the 'status' field of the relevant job list entry to JOB_BACKGROUND
        //    (as it was JOB_STOPPED before this)
            
        // 1. 
        char *index = strvec_get(tokens, 1); 
        int job_index = atoi(index);
        if (job_index >= jobs->length) {
            fprintf(stderr, "Job index out of bounds\n"); 
            return -1;
        }
        job_t *job = job_list_get(jobs, job_index);
        if (job == NULL) {
            fprintf(stderr, "Job not found"); 
            return -1;
        }
        job->status = JOB_BACKGROUND;
        // 2.
        if (kill(job->pid, SIGCONT) == -1) {
            perror("kill");
            return -1;
        }
    }
    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // TODO Task 6: Wait for a specific job to stop or terminate
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    // 2. Make sure the job's status is JOB_BACKGROUND (no sense waiting for a stopped job)
    // 3. Use waitpid() to wait for the job to terminate, as you have in resume_job() and main().
    // 4. If the process terminates (is not stopped by a signal) remove it from the jobs list
    char *index = strvec_get(tokens, 1); 
    int job_index = atoi(index);
    if (job_index >= jobs->length) {
        fprintf(stderr, "Job index out of bounds\n"); 
        return -1;
    }
    job_t *job = job_list_get(jobs, job_index);
    if (job == NULL) {
        fprintf(stderr, "Job not found"); 
        return -1;
    }
    if (job->status != JOB_BACKGROUND) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1; 
    }
    int status;
    if (waitpid(job->pid, &status, WUNTRACED) == -1) {
        perror("waitpid");
        return -1;
    }
    // remove from job list
    if (WIFEXITED(status)) {
        if (job_list_remove(jobs, job_index) == -1) {
            perror("job_list_remove");
            return -1;
        }
    }
    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    // TODO Task 6: Wait for all background jobs to stop or terminate
    // 1. Iterate through the jobs list, ignoring any stopped jobs
    // 2. For a background job, call waitpid() with WUNTRACED.
    // 3. If the job has stopped (check with WIFSTOPPED), change its
    //    status to JOB_STOPPED. If the job has terminated, do nothing until the
    //    next step (don't attempt to remove it while iterating through the list).
    // 4. Remove all background jobs (which have all just terminated) from jobs list.
    //    Use the job_list_remove_by_status() function.
    for (int i = 0; i < jobs->length; i++) {
        job_t *job = job_list_get(jobs, i);
        if (job == NULL) {
            fprintf(stderr, "Job not found"); 
            return -1;
        }
        if (job->status == JOB_BACKGROUND) {
            int status;
            if (waitpid(job->pid, &status, WUNTRACED) == -1) {
                perror("waitpid");
                return -1;
            }
            if (WIFSTOPPED(status)) {
                job->status = JOB_STOPPED;
            }
        }
    }
    job_list_remove_by_status(jobs, JOB_BACKGROUND); 
    return 0;
}
