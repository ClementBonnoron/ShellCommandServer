#ifndef INIT_DAEMON__H
#define INIT_DAEMON__H

#define NAME_CONF_DAEMON "init/demon.conf"

//        STRUCTURES

//  check_threads : structure permettant de gérer un ensemble de threads.
typedef struct check_threads check_threads;

//  thread_ds : structure gérant un thread, permettant de savoir son état.
typedef struct thread_ds thread_ds;

// conf_d : structure permettant de contenir des informations d'un fichier
//    vérifiant cette configuration :
//        MIN_THREAD=min
//        MAX_THREAD=max
//        MAX_CONNECT_PER_THREAD=connections
//        SHM_SIZE=size
//    avec :
//      min >= max > 0
//      connections >= 0
//      size > 0
typedef struct conf_d conf_d;


//        INIT

//  set_conf : initialise une structure de type conf_d contenant les
//    informations contenues dans le fichier de nom NAME_CONF_DAEMON.
extern conf_d *set_conf(void);

//  init_threads : initialise une structure gérant min threads lancé. Peut
//    contenir au maximum max threads.
extern check_threads *init_threads(conf_d *cd);

//  print_info_threads : affiche l'état de l'ensemble des threads contenus dans
//    la structure ct.
extern void print_info_threads(check_threads *ct);

//  free_conf : libère les ressources contenues dans cd.
extern void free_conf(conf_d *cd);

//  free_check : libère les ressources contenues dans ct.
extern void free_check(check_threads *ct);

//        INFO OF CHECK_THREADS

//  get_size : retourne les nombres maximum de threads que peut gérer ct.
extern int get_size(check_threads *ct);

//  get_nb_available : retourne le nombre de thread disponible dans la structure
//    ct.
extern int get_nb_available(check_threads *ct);

// get_threads : renvoie un tableau contenant la liste des structures gérant les
//    threads de ct.
extern thread_ds **get_threads(check_threads *ct);

//  stop_threads : arrête la liste des threads contenues dans ct, et libère les
//    ressources alloué pour ceux-ci.
extern int stop_threads(check_threads *ct);

//  get_first_available_thread : renvoie le premier thread disponible. Si aucun
//    n'est disponible et ct contiens moins de max threads, alors en initialise
//    un nouveau et renvoie la structure correspondante au thread initialisé.
//    Sinon, si ct contiens déjà max threads initialisé et non disponible, alors
//    renvoie NULL.
extern thread_ds *get_first_available_thread(check_threads *ct, conf_d *conf);

//        INFO OF THREADS

//  init_thread_ds : initiliase un nouveau thread et renvoie la structure
//    correspondante à ce thread. Ce thread pourra obtenir au maximum
//    'remaining_connection' client avant de s'arrêter. Si 'remainin_connection'
//    vaut 0, alors le thread ne s'arrête jamais;
extern thread_ds *init_thread_ds(pthread_t th, int remaining_connection
  , int shm_size);

//  activate_thread : si le thread est disponible, alors active le thread pour
//    lui permettre de lire dans son espace de mémoire partagé.
extern int activate_thread(thread_ds *td);

//  get_pthread : renvoie la valeur du thread correspondant à la structure td.
extern pthread_t get_pthread(thread_ds *td);

//  get_shm_size_thread : renvoie la taille de la shm correspondante à la
//    strucuture td.
extern int get_shm_size_thread(thread_ds *td);

// remaining_connection : renvoie l'adresse de la structure td correspondant au
//    nombre restant de connexion pour le thread correspondant.
extern int *remaining_connection(thread_ds *td);

//  get_sem : renvoie l'adresse du semaphore du thread correspondant à la
//    structure td, permettant de savoir quand le thread fonctionne.
extern sem_t *get_sem(thread_ds *td);

// get_sem_can_works : renvoie l'adresse du semaphore du thread correspondant à
//    la structure td, permettant de savoir si le thread peut travailler.
extern sem_t *get_sem_can_works(thread_ds *td);

//  thread_is_available : renvoie vrai ssi le thread correspondant à la
//    structure td est disponible.
extern bool thread_is_available(thread_ds *td);

//  thread_is_available : renvoie vrai ssi le thread correspondant à la
//    structure td est fini. C'est-à-dire si il attend de ce faire libérer ses
//    ressources avec pthread_join.
extern bool thread_is_dead(thread_ds *td);

// get_available : renvoie l'adresse du boolean du thread correspondant à la
//    structure td, permettant de savoir si le thread est disponible ou non. 
extern bool *get_available(thread_ds *td);

// set_is_dead : définie la valeur du thread de  la structure td par th.
extern void set_pthread(thread_ds *td, pthread_t th);

// set_is_dead : définie si le thread correspondant à la structure td est mort,
//    selon la valeur 'value'.
extern void set_is_dead(thread_ds *td, bool value);

//  stop_thread : arrête le thread correspondant à la structure td, et libère
//    ses ressources.
extern int stop_thread(thread_ds *td);

//        PARAMETERS

//  get_min_thread : renvoie le nombre minimum de thread correspondant dans la
//    structure cd.
extern int get_min_thread(conf_d *cd);

//  get_min_thread : renvoie le nombre maximum de thread correspondant dans la
//    structure cd.
extern int get_max_thread(conf_d *cd);

//  get_min_thread : renvoie le nombre de connexion maximum de connexion d'un
//    thread correspondant dans la structure cd.
extern int get_max_connect_per_thread(conf_d *cd);

//  get_shm_size : renvoie la taille de la shm correspondante dans la structure
//    cd.
extern int get_shm_size(conf_d *cd);

//  test_conf : test les valeurs correspondantes dans la structure cd.
extern int test_conf(conf_d *cd);

#endif
