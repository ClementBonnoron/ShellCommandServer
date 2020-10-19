initdir = init/
clientdir = client_file/
serveurdir = serveur_file/
CC = gcc
CFLAGS = -std=c11 -D_XOPEN_SOURCE=500 -Wall -Wconversion -Werror -Wextra -Wpedantic -fstack-protector-all -fpie -pie -D_FORTIFY_SOURCE=2 -O
LDFLAGS = -pthread -lrt
VPATH = $(initdir) $(clientdir) $(serveurdir)
OBJECTS = $(initdir)init_daemon.o $(initdir)thread_daemon.o $(serveurdir)daemon.o
EXEC_D = serveur
EXEC_C = client

all: $(EXEC_C) $(EXEC_D)

s: $(EXEC_D)

c: $(EXEC_C)

remake:
	$(RM) $(OBJECTS) $(EXEC_D) $(EXEC_C)
	make all

clean:
	$(RM) $(OBJECTS) $(clientdir)client.o $(EXEC_D) $(EXEC_C)
	
$(EXEC_D): $(OBJECTS) $(clientdir)client.h
	$(CC) $(OBJECTS) $(LDFLAGS) -o $(EXEC_D)
	
$(EXEC_C): $(clientdir)client.o
	$(CC) $(clientdir)client.o $(LDFLAGS) -o $(EXEC_C)

tar:
	$(RM) $(OBJECTS) $(EXEC)
	tar -zcf "$(CURDIR).tar.gz" *.* $(initdir)* makefile

$(initdir)init_daemon.o: init_daemon.c init_daemon.h thread_daemon.h
$(initdir)thread_dremon.o: thread_daemon.c thread_daemon.h init_daemon.h
$(serveurdir)daemon.o: daemon.c daemon.h init_daemon.h thread_daemon.h client.h

$(clientdir)client.o: client.c client.h daemon.h thread_daemon.h
