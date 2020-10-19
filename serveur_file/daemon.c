#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>

#include "../init/init_daemon.h"
#include "../init/thread_daemon.h"
#include "daemon.h"
#include "../client_file/client.h"

static int create_pipe(void);
static void deconnect_pipe(void);
static int reading_pipe(int fd, check_threads *infos_t, conf_d *conf);
static int check_end_thread(check_threads *ct);
static int set_sig(void);
static void stop_serveur(int signum);
static void end_thread(int signum);


bool end_reading = false;
bool signal_child = false;

int main(void) {
  int value = 0;
  int fd;
  conf_d *conf;
  check_threads *infos_t;
  if ((value = set_sig()) != 0) {
    errorExit(value);
  }
  conf = set_conf();
  if (conf == NULL) {
    errorExit(3);
  }
  if (test_conf(conf) != 0) {
    errorExit(4);
  }
  if ((fd = create_pipe()) < 0) {
    free_conf(conf);
    errorExit(-fd);
  } 
  infos_t = init_threads(conf);
  if (infos_t == NULL) {
    free_conf(conf);
    close(fd);
    deconnect_pipe();
    errorExit(7);
  }
  if ((value = reading_pipe(fd, infos_t, conf)) != 0) {
    free_conf(conf);
    close(fd);
    int old_value = value;
    value = stop_threads(infos_t);
    if (value == 0) {
      value = old_value;
    }
    deconnect_pipe();
    errorExit(value);
  }
  close(fd);
  if ((value = stop_threads(infos_t)) != 0) {
    free_conf(conf);
    deconnect_pipe();
    errorExit(value);
  }
  free_conf(conf);
  deconnect_pipe();
  //
  free_conf(conf);
  return EXIT_SUCCESS;
}

static int create_pipe(void) {
  if (mkfifo(NAME_SYNC_PIPE, O_TRUNC | S_IRWXU | S_IWGRP | S_IWOTH) == -1) {
    return -5;
  }
  int fd;
  if ((fd = open(NAME_SYNC_PIPE, O_NONBLOCK | O_RDONLY)) == -1) {
    deconnect_pipe();
    return -6;
  }
  return fd;
}

static void deconnect_pipe(void) {
  if (unlink(NAME_SYNC_PIPE) == -1) {
    errorExit(9);
  }
}

static int reading_pipe(int fd, check_threads *infos_t, conf_d *conf) {
  ssize_t nb_readed = 1;
  char buffer[SIZE_MAX_TEXT_INPUT];
  strcpy(buffer, "");
  while (!end_reading) {
    if (signal_child) {
      if (check_end_thread(infos_t) != 0) {
        fprintf(stderr, "An error have been occured while closing threads !\n");
      }
      signal_child = false;
      end_reading = false;
    } else if ((nb_readed = read(fd, buffer, SIZE_MAX_TEXT_INPUT)) > 0) {
      thread_ds *first_thread = get_first_available_thread(infos_t, conf);
      char num_pid[SIZE_MAX_PIPE - strlen(SIGN_CONNECTION)];
      char num_pthread[SIZE_MAX_NAME_SHM] = "";
      char name_pipe[SIZE_MAX_PIPE] = "";
      char info[SIZE_MAX_PIPE] = "";
      int fd;
      strcpy(num_pid, "");
      strcat(num_pid, buffer + strlen(SIGN_CONNECTION));
      sprintf(name_pipe, "%s%s", NAME_OUTPUT_PIPE, num_pid);
      if (first_thread == NULL) {
        strcpy(num_pthread, SIGN_ERROR_CONNECTION);
        strcpy(info, SIGN_ERROR_CONNECTION);
      } else {
        if (sem_wait(get_sem_can_works(first_thread)) == -1) {
          return 10;
        }
        if (sem_post(get_sem_can_works(first_thread)) == -1) {
          return 10;
        }
        sprintf(num_pthread, "%ld", get_pthread(first_thread));
        sprintf(info, "%s\n%d", num_pthread, get_shm_size(conf));
        if (activate_thread(first_thread) != 0) {
          return 21;
        }
      }
      if ((fd = open(name_pipe, O_WRONLY)) == -1) {
        return 20;
      }
      if (write(fd, info, strlen(info)) != (ssize_t)strlen(info)) {
        return 22;
      }
    }
  }
  return 0;
}

int check_end_thread(check_threads *ct) {
  thread_ds **td = get_threads(ct);
  int value_error = 0;
  int *retval;
  for (int i = 0; i < get_size(ct); ++i) {
    if (td[i] != NULL && thread_is_dead(td[i])) {
      pthread_join(get_pthread(td[i]), (void **)&retval);
      thread_ds *t = td[i];
      td[i] = NULL;
      free(t);
      if (*retval != 0) {
        value_error = *retval;
      }
    }
  }
  return value_error;
}

static int set_sig(void) {
  struct sigaction action_end;
  action_end.sa_handler = stop_serveur;
  action_end.sa_flags = 0;
  struct sigaction action_thread;
  action_thread.sa_handler = end_thread;
  action_thread.sa_flags = 0;
  if (sigfillset(&action_end.sa_mask) == -1) {
    return 12;
  }
  if (sigfillset(&action_thread.sa_mask) == -1) {
    return 12;
  }
  if (sigaction(SIGINT, &action_end, NULL) == -1) {
    return 13;
  }
  if (sigaction(SIGQUIT, &action_end, NULL) == -1) {
    return 13;
  }
  if (sigaction(SIGCHLD, &action_thread, NULL) == -1) {
    return 13;
  }
  return 0;
}

static void stop_serveur(int signum) {
  if (signum < 0) {
    errorExit(14);
  }
  end_reading = true;
}

static void end_thread(int signum) {
  if (signum < 0) {
    errorExit(14);
  }
  signal_child = true;
}

int errorExit(int nb) {
  switch(nb) {
    case 3:
      fprintf(stderr, "Error while reading file demon.conf !\n");
      break;
    case 4:
      fprintf(stderr, "Error with the configuration of demon.conf !\n");
      break;
    case 5:
      fprintf(stderr, "Error while creating pipe \"" NAME_SYNC_PIPE "\" !\n");
      break;
    case 6:
      fprintf(stderr, "Error while opening the pipe !\n");
      break;
    case 7:
      fprintf(stderr, "Error while creating the first threads !\n");
      break;
    case 9:
      fprintf(stderr, "Error while unlinking the pipe !\n");
      break;
    case 10:
      fprintf(stderr, "Error while unlocking / locking sem for a thread !\n");
      break;
    case 11:
      fprintf(stderr, "Error while stoping sem for thread !\n");
      break;
    case 12:
      fprintf(stderr, "Error while creating new sigaction !\n");
      break;
    case 13:
      fprintf(stderr, "Error while setting action !\n");
      break;
    case 14:
      fprintf(stderr, "Error while getting an action !\n");
      break;
    case 16:
      fprintf(stderr, "Error while deleting an shm !\n");
      break;
    case 17:
      fprintf(stderr, "Error while unlinking an semaphore !\n");
      break;
    case 20:
      fprintf(stderr, "Error while opening an input pipe !\n");
      break;
    case 21:
      fprintf(stderr, "Error while unlocking a thread !\n");
      break;
    case 22:
      fprintf(stderr, "Error while writing into an input pipe !\n");
      break;
    default:
      fprintf(stderr,  "No error corresponding with this number : %d !\n", nb);
      break;
  }
  exit(EXIT_FAILURE);
}


