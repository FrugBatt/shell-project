#include<string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include "global.h"

// This is the file that you should work on.

// declaration
int execute (struct cmd *cmd);

// name of the program, to be printed in several places
#define NAME "hugoshell"


// Some helpful functions

void errmsg (char *msg)
{
	fprintf(stderr,"error: %s\n",msg);
}

// Annule le ^C
void sig_hand() {
  rl_crlf();
  rl_reset_line_state();
  rl_replace_line("", 0);
  rl_redisplay();
}

// apply_redirects() should modify the file descriptors for standard
// input/output/error (0/1/2) of the current process to the files
// whose names are given in cmd->input/output/error.
// append is like output but the file should be extended rather
// than overwritten.

void apply_redirects (struct cmd *cmd)
{
  if(cmd->output != NULL) {
    int f = open(cmd->output, O_WRONLY | O_CREAT, 0644);
    dup2(f, STDOUT_FILENO);
  } else if (cmd->append != NULL) {
    int f = open(cmd->append, O_WRONLY | O_CREAT | O_APPEND, 0644);
    dup2(f, STDOUT_FILENO);
  } else if (cmd->error != NULL) {
    int f = open(cmd->error, O_WRONLY | O_CREAT | O_APPEND, 0644);
    dup2(f, STDERR_FILENO);
  } else if (cmd->input != NULL) {
    int f = open(cmd->input, O_RDONLY);
    dup2(f, STDIN_FILENO);
  }
}


int perform_cd(struct cmd *cmd) {
  int fd_err = STDERR_FILENO;
  if (cmd->error != NULL) fd_err = open(cmd->error, O_WRONLY | O_CREAT | O_APPEND, 0644);

  char *dir = cmd->args[1];
  if (cmd->args[2] != NULL) {
    dprintf(fd_err, "cd: too many arguments");
    return -1;
  }
  if (dir == NULL) dir = getenv("HOME");
  
  if (chdir(dir)) {
    dprintf(fd_err, "cd: file %s doesn't exist\n", dir);
    return -1;
  }

  return 0;
}

int perform_history(struct cmd *cmd) {
  int fd_err = STDERR_FILENO;
  if (cmd->error != NULL) fd_err = open(cmd->error, O_WRONLY | O_CREAT | O_APPEND, 0644);

  HISTORY_STATE *myhist = history_get_history_state();
  HIST_ENTRY **mylist = history_list();

  int nb_entries = myhist->length < 500 ? myhist->length : 500;
  if (cmd->args[1] != NULL) nb_entries = myhist->length < atoi(cmd->args[1]) ? myhist->length : atoi(cmd->args[1]);
  if (cmd->args[2] != NULL) {
    dprintf(fd_err, "history: too many arguments\n");
    return -1;
  }

  int first_entry = myhist->length - nb_entries;
  printf ("session history:\n");
  for (int i = 0; i < nb_entries; i++) {
      printf ("  %s\n", mylist[first_entry + i]->line);
  }
  return 0;
}

// The function execute() takes a command parsed at the command line.
// The structure of the command is explained in output.c.
// Returns the exit code of the command in question.

int execute (struct cmd *cmd)
{
	switch (cmd->type)
	{
	    case C_PLAIN:
        if (strcmp(cmd->args[0],"cd") == 0) {
          return perform_cd(cmd);
        } else if (strcmp(cmd->args[0], "history") == 0) {
          return perform_history(cmd);
        }

        if (fork()) { // PARENT PROCESS
          int status;
          wait(&status);
          return WEXITSTATUS(status);
        } else { // CHILD PROCESS
          apply_redirects(cmd);
          execvp(cmd->args[0], cmd->args);
          fprintf(stderr, "command %s doesn't exist\n", cmd->args[0]);
          exit(-1);
        }
	    case C_SEQ:
        execute(cmd->left);
        return execute(cmd->right);
	    case C_AND:
        int lexitand = execute(cmd->left);
        if (!lexitand) return execute(cmd->right);
        return lexitand;
	    case C_OR:
        int lexitor = execute(cmd->left);
        if (lexitor) return execute(cmd->right);
        return lexitor;
	    case C_PIPE:
        int fd[2];
        pipe(fd);

        if (fork()) {
          close(fd[1]);
          wait(NULL);
          if (fork()) {
            close(fd[0]);
            int status;
            wait(&status);
            return WEXITSTATUS(status);
          } else {
            dup2(fd[0], STDIN_FILENO);
            exit(execute(cmd->right));
          }
        } else {
          dup2(fd[1], STDOUT_FILENO);
          exit(execute(cmd->left));
        }
        return 0;
	    case C_VOID:
        if (fork()) {
          int status;
          wait(&status);
          return WEXITSTATUS(status);
        } else {
          apply_redirects(cmd);
          exit(execute(cmd->left));
        }
		errmsg("I do not know how to do this, please help me!");
		return -1;
	}

	// Just to satisfy the compiler
	errmsg("This cannot happen!");
	return -1;
}

int main (int argc, char **argv)
{
  signal(SIGINT, sig_hand);

	char *prompt = malloc(strlen(NAME)+3);
	printf("welcome to %s!\n", NAME);
	sprintf(prompt,"%s> ", NAME);

	while (1)
	{
		char *line = readline(prompt);
		if (!line) break;	// user pressed Ctrl+D; quit shell
		if (!*line) continue;	// empty line

		add_history (line);	// add line to history

		struct cmd *cmd = parser(line);
		if (!cmd) continue;	// some parse error occurred; ignore
		// output(cmd,0);	// activate this for debugging
		execute(cmd);
	}

	printf("goodbye!\n");
	return 0;
}
