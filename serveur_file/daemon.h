#ifndef DAEMON__H
#define DAEMON__H

#define NAME_SYNC_PIPE "/tmp/pipe_connection"
#define SIGN_CONNECTION "SYNC_"
#define SIGN_ERROR_CONNECTION "RST"
#define SIZE_MAX_TEXT_INPUT 255

//  errorExit : affiche un message d'erreur selon le numéro passé en paramètre,
//    et appelle exit(EXIT_FAILURE)
extern int errorExit(int nb);

#endif
