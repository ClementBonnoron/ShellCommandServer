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

#include "client.h"
#include "../init/thread_daemon.h"
#include "../serveur_file/daemon.h"


static int create_pipe(char *name_pipe);
static void deconnect_pipe(char *name_pipe);
static int send_request(char *name_pipe);
static int read_answer(int fd);
static volatile request_shm *connect_to_shm(char *name_shm, long int shm_size);
static volatile sem_t *open_sem(char *name);
static void clear_shm(request_shm *shm, int size_shm);
static void set_sig(void);
static void do_nothing(int signum);

int main(void) {
  set_sig();
  char name_pipe[SIZE_MAX_PIPE] = "";
  sprintf(name_pipe, "%s%d", NAME_OUTPUT_PIPE, getpid());
  int fd = create_pipe(name_pipe);
  if (send_request(NAME_SYNC_PIPE) != 0) {
    fprintf(stderr, "The server is not connected\n");
    deconnect_pipe(name_pipe);
    return EXIT_FAILURE;
  }
  int v;
  if ((v = read_answer(fd)) != 0) {
    if (v == 1) {
      fprintf(stderr, "Error! \nThe server cannot get another connection\n");
    } else if (v == -1) {
      fprintf(stderr, "Not enough memory!\n");
    } else if (v == -2) {
      fprintf(stderr, "Error with unlocking semaphore!\n");
    } else if (v == 5) {
      fprintf(stderr, "Error while waiting a semaphor !\n");
    } else if (v == 6) {
      fprintf(stderr, "The server must have been disconnected,"
        " try to reconnect again\n");
    } else {
      fprintf(stderr, "Error while getting an answer\n");
    }
    deconnect_pipe(name_pipe);
    return EXIT_FAILURE;
  }
  fprintf(stderr, "Good bye !\n");
  if (close(fd) == -1) {
    return EXIT_FAILURE;
  }
  deconnect_pipe(name_pipe);
  return EXIT_SUCCESS;
}

static int create_pipe(char *name_pipe) {
  if (mkfifo(name_pipe, O_TRUNC | S_IRWXU | S_IWGRP | S_IWOTH) == -1) {
    perror("mkfifo");
    return -1;
  }
  int fd;
  if ((fd = open(name_pipe, O_NONBLOCK | O_RDWR)) == -1 ) {
    deconnect_pipe(name_pipe);
    perror("open : create_pipe");
    return -1;
  }
  return fd;
}

static void deconnect_pipe(char *name_pipe) {
  if (unlink(name_pipe) == -1) {
    perror("unlink");
    exit(3);
  }
}

static int send_request(char *name_pipe) {
  char request[SIZE_MAX_TEXT_OUTPUT];
  strcpy(request, "");
  sprintf(request, "%s%d", SIGN_CONNECTION, getpid());
  int fd = -1;
  if ((fd = open(name_pipe, O_WRONLY)) == -1) {
    return 1;
  }
  ssize_t nb_write = 1;
  if ((nb_write = write(fd, request, strlen(request) + 1)) < 0) {
    return 2;
  }
  fprintf(stderr, "Connection...\n");
  return 0;
}

static int read_answer(int fd) {
  bool have_answer = false;
  ssize_t nb_readed = 1;
  char buffer[SIZE_MAX_TEXT_OUTPUT];
  while (!have_answer) {
    if ((nb_readed = read(fd, buffer, SIZE_MAX_TEXT_OUTPUT)) > 0) {
      if (strncmp(buffer, SIGN_ERROR_CONNECTION,
          strlen(SIGN_ERROR_CONNECTION)) == 0) {
        return 1;
      }
      char *pthread_value = NULL;
      pthread_value = strtok(buffer, "\n");
      char *size_c = "";
      size_c = strtok(NULL, "");
      long int size = 0;
      if (pthread_value == NULL) {
        return 2;
      }
      if (size_c == NULL) {
        return 3;
      }
      size = strtol(size_c, NULL, 10);
      if (errno == ERANGE) {
        return 4;
      }
      have_answer = true;
      char name_shm[SIZE_MAX_NAME_SHM] = "";
      char name_request[SIZE_MAX_NAME_SEM] = "";
      char name_answer[SIZE_MAX_NAME_SEM] = "";
      sprintf(name_request, "%s%s", NAME_SEM_REQUEST, pthread_value);
      sprintf(name_answer, "%s%s", NAME_SEM_ANSWER, pthread_value);
      sprintf(name_shm, "%s%s", NAME_SHM, pthread_value);
      volatile request_shm *shm = connect_to_shm(name_shm, size);
      if (shm == NULL) {
        fprintf(stderr, "Error shm\n");
        return -1;
      }
      volatile sem_t *sem_answer = open_sem(name_answer);
      if (sem_answer == NULL) {
        fprintf(stderr, "Error answer\n");
        return -1;
      }
      volatile sem_t *sem_request = open_sem(name_request);
      if (sem_request == NULL) {
        fprintf(stderr, "Error request\n");
        return -1;
      }
      fprintf(stderr, "Connected !\n");
      char buffer[size];
      char request[size];
      strcpy(request, "");
      strcpy(buffer, "");
      ssize_t nb_readed = 0;
      while ((nb_readed = read(STDIN_FILENO, buffer, (size_t) size)) > 0 &&
           strncmp(buffer, REQUEST_END_CONNECTION, strlen(REQUEST_END_CONNECTION)) != 0){
        if (nb_readed > 1) {
          if (sem_wait((sem_t *)sem_answer) == -1) {
            strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
            shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
            sem_post((sem_t *)sem_request);
            return 5;
          }
          if (strcmp(request, "") != 0 &&
              strncmp(request, (char *)shm->buffer, (size_t) shm->length_ra) == 0) {
            strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
            shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
            if (sem_post((sem_t *)sem_request) == -1) {
              return 5;
            }
            return 6;
          }
          strcpy(request, "");
          clear_shm((request_shm *)shm, shm->length_ra);
          strncpy(request, buffer, (size_t)nb_readed + 1);
          strcpy((char *)shm->buffer, request);
          shm->length_ra = (int)nb_readed - 1;
          fprintf(stdout, "\n");
          if (sem_post((sem_t *)sem_request) == -1) {
            strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
            shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
            return 5;
          }
          if (sem_wait((sem_t *)sem_answer) == -1) {
            strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
            shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
            sem_post((sem_t *)sem_request);
            return 5;
          }
          bool have_get_answer = false;
          while (shm->still_answser) {
            fprintf(stdout, "%s", shm->buffer);
            have_get_answer = true;
            if (sem_post((sem_t *)sem_request) == -1) {
              strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
              shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
              return 5;
            }
            if (sem_wait((sem_t *)sem_answer) == -1) {
              strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
              shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
              sem_post((sem_t *)sem_request);
              return 5;
            }
          }
          if (!have_get_answer) {
            fprintf(stdout, "%s\n", shm->buffer);
          } else {
            fprintf(stdout, "\n");
          }
          if (sem_post((sem_t *)sem_answer) == -1) {
            strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
            shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
            sem_post((sem_t *)sem_request);
            return 5;
          }
        }
      } 
      if (strncmp(request, REQUEST_END_CONNECTION, strlen(REQUEST_END_CONNECTION)) != 0) {
        if (sem_wait((sem_t *)sem_answer) == -1) {
          return 5;
        }
        fprintf(stderr, "Deconnection.\n");
        strcpy((char *)shm->buffer, REQUEST_END_CONNECTION);
        shm->length_ra = (int)strlen(REQUEST_END_CONNECTION);
        if (sem_post((sem_t *)sem_request) == -1) {
          return 5;
        }
      }
    }
  }
  return 0;
}

static volatile request_shm *connect_to_shm(char *name_shm, long int shm_size) {
  int shm_fd;
  if ((shm_fd = shm_open(name_shm, O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO)) == -1) {
    return NULL;
  }
  volatile request_shm *rdv = mmap(NULL,
    (sizeof(request_shm) + (size_t)shm_size), PROT_READ | PROT_WRITE,
    MAP_SHARED, shm_fd, 0);
  if (rdv == MAP_FAILED) {
    close(shm_fd);
    return NULL;
  }
  if (close(shm_fd) == -1) {
    return NULL;
  }
  return rdv;
}

static volatile sem_t *open_sem(char *name) {
  volatile sem_t *s = sem_open(name, 0);
  if (s == SEM_FAILED) {
    return NULL;
  }
  return s;
}

static void clear_shm(request_shm *shm, int size_shm) {
  for (int i = 0; i < size_shm; ++i) {
    shm->buffer[i] = '\0';
  }
}

static void set_sig(void) {
  struct sigaction action;
  action.sa_handler = do_nothing;
  action.sa_flags = 0;
  if (sigfillset(&action.sa_mask) == -1) {
    fprintf(stderr, "Error while setting mask !\n");
    exit(-1);
  }
  if (sigaction(SIGINT, &action, NULL) == -1) {
    fprintf(stderr, "Error while setting mask !\n");
    exit(-1);
  }
  if (sigaction(SIGQUIT, &action, NULL) == -1) {
    fprintf(stderr, "Error while setting mask !\n");
    exit(-1);
  }
}

static void do_nothing(int signum) {
  if (signum < 0) {
    fprintf(stderr, "Error signum !\n");
    exit(-2);
  }
}


