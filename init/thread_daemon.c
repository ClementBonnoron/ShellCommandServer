#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include "../serveur_file/daemon.h"
#include "init_daemon.h"
#include "thread_daemon.h"

static volatile request_shm *create_private_shm(thread_ds *info);
static volatile sem_t *open_sem(char *name, int value);
static int call_shell(request_shm *shm, volatile sem_t *sem_answer,
  volatile sem_t *sem_request);
static void clear_array(char *array, int size);
static int nb_arg_command(char *command);

bool lost_last_client = false;

void *run_thread(void *arg) {
  int value_return = 0;
  thread_ds *info = (thread_ds *)arg;
  set_pthread(info, pthread_self());
  volatile request_shm *shm = create_private_shm(info);
  if (shm == NULL) {
    value_return = 12;
    goto dispose;
  }
  volatile sem_t *sem_answer = NULL;
  volatile sem_t *sem_request = NULL;
  char name_request[SIZE_MAX_NAME_SEM] = "";
  char name_answer[SIZE_MAX_NAME_SEM] = "";
  sprintf(name_request, "%s%ld", NAME_SEM_REQUEST, get_pthread(info));
  sprintf(name_answer, "%s%ld", NAME_SEM_ANSWER, get_pthread(info));
  sem_answer = open_sem(name_answer, 1);
  sem_request = open_sem(name_request, 0);
  if (sem_answer == NULL || sem_request == NULL) {
    value_return = 12;
    goto dispose;
  }
  int *rc = remaining_connection(info);
  bool *available = get_available(info);
  bool is_always_available = (*rc == 0 ? true : false);
  if (sem_post(get_sem_can_works(info)) == -1) {
    if (errno != EINTR) {
      value_return = 10;
    }
    goto dispose;
  }
  while (*rc > 0 || (is_always_available && *rc != -1)) {
    if (sem_wait(get_sem(info)) == -1) {
      if (errno != EINTR) {
        value_return = 10;
      }
      goto dispose;
    }
    if (*rc != -1) {
      if (sem_wait(get_sem_can_works(info)) == -1) {
        if (errno != EINTR) {
          value_return = 10;
        }
        goto dispose;
      }
      *available = false;
      char request[get_shm_size_thread(info)];
      strcpy(request, "");
      while ((strncmp(request, REQUEST_END_CONNECTION,
          strlen(REQUEST_END_CONNECTION)) != 0) && !lost_last_client) {
        if (sem_wait((sem_t *)sem_request) == -1) {
          strcpy((char *)shm->buffer, ERROR_SERVER);
          shm->length_ra = (int) strlen(ERROR_REQUEST);
          if (sem_post((sem_t *)sem_answer) == -1) {
            if (errno != EINTR) {
              value_return = 10;
            }
            goto dispose;
          }
          if (errno != EINTR) {
            value_return = 10;
          }
          goto dispose;
        }
        clear_array(request, get_shm_size_thread(info));
        strncpy(request, (char *)shm->buffer, (size_t)shm->length_ra);
        int value;
        if ((value = call_shell((request_shm *)shm, sem_answer, sem_request)) != 0) {
          if (value == 3) {
            strcpy((char *)shm->buffer, ERROR_REQUEST);
            shm->length_ra = (int) strlen(ERROR_REQUEST);
          } else {
            strcpy((char *)shm->buffer, ERROR_SERVER);
            shm->length_ra = (int) strlen(ERROR_SERVER);
            if (sem_post((sem_t *)sem_answer) == -1) {
              if (errno != EINTR) {
                value_return = 10;
              }
              goto dispose;
            }
            value_return = 11;
            goto dispose;
          }
        }
        if (sem_post((sem_t *)sem_answer) == -1) {
          strcpy((char *)shm->buffer, ERROR_SERVER);
          shm->length_ra = (int) strlen(ERROR_REQUEST);
          if (sem_post((sem_t *)sem_answer) == -1) {
            if (errno != EINTR) {
              value_return = 10;
            }
            goto dispose;
          }
          if (errno != EINTR) {
            value_return = 10;
          }
          goto dispose;
        }
      }
      clear_array((char *)shm->buffer, get_shm_size_thread(info));
      *available = true;
      if (!is_always_available) {
        --*rc;
      }
      if (sem_post(get_sem_can_works(info)) == -1) {
        if (errno != EINTR) {
          value_return = 10;
        }
        goto dispose;
      }
    }
  }
dispose:
  set_is_dead(info, true);
  if (name_answer != NULL && sem_unlink(name_answer) == -1) {
    value_return = 17;
  }
  if (name_request != NULL && sem_unlink(name_request) == -1) {
    value_return = 17;
  }
  if (shm != NULL && shm_unlink((char *)shm->name) == -1) {
    value_return = 16;
  }
  pthread_exit((void *) &value_return);
}

static volatile request_shm *create_private_shm(thread_ds *info) {
  char name[SIZE_MAX_NAME_SHM];
  sprintf(name, "%s%ld", NAME_SHM, get_pthread(info));
  size_t size_shm = sizeof(request_shm) + (size_t)get_shm_size_thread(info);
  int shm_fd;
  if ((shm_fd = shm_open(name, O_CREAT | O_RDWR,
      S_IRWXU | S_IRWXG | S_IRWXO)) == -1) {
    return NULL;
  }
  if (ftruncate(shm_fd, (off_t)size_shm) == -1) {
    if (close(shm_fd) == -1) {
      return NULL;
    }
    return NULL;
  }
  volatile request_shm *rdv = mmap(NULL, size_shm,
    PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (rdv == MAP_FAILED) {
    if (close(shm_fd) == -1) {
      return NULL;
    }
    return NULL;
  }
  mprotect((void *)rdv, sizeof(request_shm), PROT_WRITE);
  rdv->length_ra = 0;
  rdv->size = get_shm_size_thread(info);
  rdv->still_answser = false;
  clear_array((char *)rdv->buffer, get_shm_size_thread(info));
  strcpy((char *)rdv->name, name);
  if (close(shm_fd) == -1) {
    return NULL;
  }
  return rdv;
}

static volatile sem_t *open_sem(char *name, int value) {
  volatile sem_t *s = NULL;
  s = sem_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IRWXO, value);
  if (s == SEM_FAILED) {
    return NULL;
  }
  return s;
}

static int call_shell(request_shm *shm, volatile sem_t *sem_answer,
  volatile sem_t *sem_request) {
  pid_t pt;
  ssize_t nb_readed;
  int tube[2];
  int size = nb_arg_command(shm->buffer);
  int index = 1;
  int waitstatus = 0;
  char *args[size + 1];
  char *arg;
  char *posn;
  char command[shm->size];
  char buffer[shm->size];
  clear_array(command, shm->size);
  strncpy(command, shm->buffer, (size_t)shm->length_ra);
  args[size] = NULL;
  arg = strtok_r(command, " ", &posn);
  if (arg == NULL) {
    return 1;
  }
  args[0] = arg;
  while ((arg = strtok_r(NULL, " ", &posn)) != NULL) {
    args[index] = arg;
    ++index;
  }
  if (pipe(tube) == -1) {
    return 2;
  }
  switch((pt = fork())) {
    case -1:
      return -1;
    case 0:
      dup2(tube[1], STDOUT_FILENO);
      close(STDERR_FILENO);
      if (close(tube[0]) == -1) {
        exit(1);
      }
      if (execvp(args[0], args) != 0) {
        if (close(tube[1]) == -1) {
          exit(1);
        }
        exit(1);
      }
      exit(0);
    default:
      waitpid(pt, &waitstatus, 0);
      if (close(tube[1]) == -1) {
        return 2;
      }
      if (WIFEXITED(waitstatus) != 0) {
        if (WEXITSTATUS(waitstatus) == 1) {
          return 3;
        }
      } else {
        return 3;
      }
      shm->still_answser = true;
      if (sem_post((sem_t *)sem_request) == -1) {
        return 4;
      }
      while ((nb_readed = read(tube[0], buffer, (size_t) (shm->size - 2))) > 0) {
        clear_array(shm->buffer, shm->size);
        if (sem_wait((sem_t *)sem_request) == -1) {
          return 4;
        }
        strncpy(shm->buffer, buffer, (size_t) nb_readed);
        shm->length_ra = (int) nb_readed;
        if (sem_post((sem_t *)sem_answer) == -1) {
          return 4;
        }
      }
      if (sem_wait((sem_t *)sem_request) == -1) {
        return 4;
      }
      shm->still_answser = false;
      break;
  }
  return 0;
}

static void clear_array(char *array, int size) {
  for (int i = 0; i < size; ++i) {
    array[i] = '\0';
  }
}

static int nb_arg_command(char *command) {
  int nb = 0;
  int index = 0;
  while (command[index] != '\0') {
    while (command[index] != '\0' && command[index] != ' ') {
      ++index;
    }
    nb += 1;
    while (command[index] == ' ') {
      ++index;
    }
  }
  return nb;
}

