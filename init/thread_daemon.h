#ifndef THREAD_DAEMON__H
#define THREAD_DAEMON__H

#include "./init_daemon.h"

#define REQUEST_END_CONNECTION "END"
#define ERROR_REQUEST "This isn't a valid request !"
#define ERROR_SERVER "An error has occured with server !"

#define NAME_SHM "/shm_thread_"
#define NAME_SEM_REQUEST "/sem_request_"
#define NAME_SEM_ANSWER "/sem_answer_"
#define SIZE_MAX_NAME_SHM 255
#define SIZE_MAX_NAME_SEM 255

#define SEC_MAX_WITHOUT_REQUEST 10

//           STRUCTURES

//  request_shm : structure permettant un échange de données entre le thread et
//    un client. Le paramètre name contient le nom de la shm, size la taille du
//    buffer, length_ra la longueur de la requête contenant dans le buffer,
//    still_answer est vrai ssi le thread a encore une réponse à donner.
typedef struct request_shm request_shm;

struct request_shm {
  char name[SIZE_MAX_NAME_SHM];
  int size;
  int length_ra;
  bool still_answser;
  char buffer[];
};

// run_thread : routine de démarrage d'un thread. Ce thread récupère une
//    commande shell dans sa shm et met sa réponse dans la shm. Si la longueur
//    de la shm n'est pas assez grande pour donner la réponse en une fois, alors
//    still_answer ce met à vrai tant qu'il reste une partie de la réponse.
extern void *run_thread(void *arg);

#endif
