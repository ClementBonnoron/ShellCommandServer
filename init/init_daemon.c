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
#include <pwd.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include "../serveur_file/daemon.h"
#include "init_daemon.h"
#include "thread_daemon.h"

#define MIN_TH_FILE "MIN_THREAD="
#define MAX_TH_FILE "MAX_THREAD="
#define MAX_CPTH_FILE "MAX_CONNECT_PER_THREAD="
#define SHM_SIZE_FILE "SHM_SIZE="
#define NB_PARAM_FILE 4
#define SIZE_PARAM_FILE 64
#define BASE_FILE 10

// STRUCTURES

struct conf_d {
  int min_thread;
  int max_thread;
  int max_connect_per_thread;
  int shm_size;
};

struct check_threads {
  int size;
  int available;
  thread_ds **threads;
};

struct thread_ds {
  pthread_t th;
  int remaining_connection;
  sem_t working;
  sem_t can_works;
  bool is_dead;
  bool available;
  int shm_size;
};

//  FONCTIONS INIT

check_threads *init_threads(conf_d *cd) {
  check_threads *ct = malloc(sizeof(check_threads)
      + (size_t)cd->max_thread * sizeof(thread_ds));
  if (ct == NULL) {
    return NULL;
  }
  ct->threads = malloc(sizeof(thread_ds) * (size_t)get_max_thread(cd));
  if (ct->threads == NULL) {
    free(ct);
    return NULL;
  }
  ct->size = cd->max_thread;
  ct->available = cd->min_thread;
  pthread_t th;
  int errnum;
  for (int i = 0; i < get_min_thread(cd); ++i) {
    int num = cd->max_connect_per_thread;
    ct->threads[i] = init_thread_ds((pthread_t) -1, num, cd->shm_size);
    if (ct->threads[i] == NULL) {
      free_check(ct);
      return NULL;
    }
    if ((errnum = pthread_create(&th, NULL, run_thread, ct->threads[i])) != 0) {
      free_check(ct);
      return NULL;
    }
  }
  for (int i = get_min_thread(cd); i < get_max_thread(cd); i++) {
    ct->threads[i] = NULL;
  }
  return ct;
}

void print_info_threads(check_threads *ct) {
  for (int i = 0; i < ct->size; ++i) {
    if (ct->threads[i] != NULL) {
      fprintf(stdout, "thread %d : %ld (%s)\n", i, ct->threads[i]->th,
        (ct->threads[i]->th == (pthread_t) -1 ? "invalide" :
          (ct->threads[i]->available ? "disponible" : "non disponible")));
    }
  }
}

void free_check(check_threads *ct) {
  for (int i = 0; i < ct->size; ++i) {
    free(ct->threads[i]);
  }
  free(ct->threads);
  free(ct);
}

int get_size(check_threads *ct) {
  return ct->size;
}

int get_nb_available(check_threads *ct) {
  return ct->available;
}

thread_ds **get_threads(check_threads *ct) {
  return ct->threads;
}

int stop_threads(check_threads *ct) {
  for (int k = 0; k < ct->size; ++k) {
    if (ct->threads[k] != NULL) {
      int value;
      if ((value = stop_thread(ct->threads[k])) != 0) {
        return value;
      }
    }
  }
  free(ct->threads);
  free(ct);
  return 0;
}

thread_ds *get_first_available_thread(check_threads *ct, conf_d *conf) {
  int index = 0;
  thread_ds **threads = ct->threads;
  while (index < ct->size && threads[index] != NULL) {
    if (thread_is_available(threads[index])) {
      return threads[index];
    }
    ++index;
  }
  if (index < ct->size) {
    pthread_t th;
    int errnum;
    int num = conf->max_connect_per_thread;
    ct->threads[index] = init_thread_ds((pthread_t) -1, num, conf->shm_size);
    if (ct->threads[index] == NULL) {
      return NULL;
    }
    if ((errnum = pthread_create(&th, NULL, run_thread, ct->threads[index])) != 0) {
      return NULL;
    }
    set_pthread(threads[index], th);
    return threads[index];
  }
  return NULL;
}

thread_ds *init_thread_ds(pthread_t th, int remaining_connection,
    int shm_size) {
  thread_ds *td = malloc(sizeof(thread_ds));
  if (td == NULL) {
    return NULL;
  }
  td->th = th;
  td->remaining_connection = remaining_connection;
  if (sem_init(&td->working, 1, 0) == -1) {
    perror("sem_init");
    free(td);
    return NULL;
  }
  if (sem_init(&td->can_works, 1, 0) == -1) {
    perror("sem_init");
    free(td);
    return NULL;
  }
  td->is_dead = false;
  td->available = true;
  td->shm_size = shm_size;
  return td;
}


//  FONCTIONS WITH THREAD_DS

int activate_thread(thread_ds *td) {
  if (!thread_is_available(td)) {
    return 1;
  }
  if (sem_post(&td->working) == -1) {
    return 10;
  }
  return 0;
}

pthread_t get_pthread(thread_ds *td) {
  return td->th;
}

int *remaining_connection(thread_ds *td) {
  return &td->remaining_connection;
}

sem_t *get_sem(thread_ds *td) {
  return &td->working;
}

sem_t *get_sem_can_works(thread_ds *td) {
  return &td->can_works;
}

bool *get_available(thread_ds *td) {
  return &td->available;
}

bool thread_is_available(thread_ds *td) {
  return (td->th != (pthread_t) -1) && (td->available);
}

bool thread_is_dead(thread_ds *td) {
  return td->is_dead;
}

void set_pthread(thread_ds *td, pthread_t th) {
  td->th = th;
}

void set_is_dead(thread_ds *td, bool value) {
  td->is_dead = value;
}
  

int stop_thread(thread_ds *td) {
  int old_remaining_connexion = td->remaining_connection;
  if (sem_wait(get_sem_can_works(td)) == -1) {
    return 1;
  }
  if (sem_post(get_sem_can_works(td)) == -1) {
    return 1;
  }
  if (old_remaining_connexion == 0 ||
      (old_remaining_connexion != 0 && td->remaining_connection != 0)) {
    td->remaining_connection = -1;
    if (sem_post(&td->working) == -1) {
      return 1;
    }
  }
  int *value;
  pthread_join(td->th, (void **)&value);
  free(td);
  return *value;
}


//  FONCTION WITH CONF

conf_d *set_conf(void) {
  int fd_conf = open(NAME_CONF_DAEMON, O_RDONLY);
  if (fd_conf== -1) {
    return NULL;
  }
  conf_d *conf = malloc(sizeof(conf_d));
  if (conf == NULL) {
    close(fd_conf);
    return NULL;
  }
  char *parameters[NB_PARAM_FILE] = {
    MIN_TH_FILE, MAX_TH_FILE, MAX_CPTH_FILE, SHM_SIZE_FILE};
  for (int i = 0; i < NB_PARAM_FILE; i++) {
    char value[SIZE_PARAM_FILE] = "";
    int index = 0;
    lseek(fd_conf, (off_t)strlen(parameters[i]), SEEK_CUR);
    char character[1];
    while (read(fd_conf, character, 1) > 0 && character[0] != '\n') {
      value[index] = character[0];
      ++index;
    }
    if (i == NB_PARAM_FILE - 1 && character[0] != '\0' && character[0] != '\n'
        && character[0] != EOF) {
      close(fd_conf);
      free(conf);
      return NULL;
    }
    int k = (int)strtol(value, NULL, BASE_FILE);
    if (errno == ERANGE) {
      close(fd_conf);
      free(conf);
      return NULL;
    }
    switch(i) {
      case 0:
        conf->min_thread = k;
        break;
      case 1:
        conf->max_thread = k;
        break;
      case 2:
        conf->max_connect_per_thread = k;
        break;
      case 3:
        conf->shm_size = k;
        break;
    }
  }
  return conf;
}

void free_conf(conf_d *cd) {
  free(cd);
}

int get_min_thread(conf_d *cd) {
  return cd->min_thread;
}

int get_max_thread(conf_d *cd) {
  return cd->max_thread;
}

int get_max_connect_per_thread(conf_d *cd) {
  return cd->max_connect_per_thread;
}

int get_shm_size(conf_d *cd) {
  return cd->shm_size;
}

int get_shm_size_thread(thread_ds *td) {
  return td->shm_size;
}

int test_conf(conf_d *cd) {
  if (cd->min_thread > cd->max_thread) {
    return -1;
  } else if (cd->min_thread < 0 || cd->max_thread <= 0 ||
      cd->max_connect_per_thread < 0 || cd->shm_size <= 0) {
    return -2;
  }
  return 0;
}
