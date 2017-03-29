/*
Kevin Corizi, 501942
A.A. 2015/2016

Parte server dell'applicazione CombinazioneSegreta.
Il file non riporta errori o warning se compilato con
	gcc -Wall -pedantic -std=c89 comb_server.c -o comb_server
*/

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <limits.h>

/* Definizione degli stati di operatività dei client connessi al server.
	Il server eseguirà per ogni client connesso operazioni diverse
	a seconda dello stato in cui esso si trova al momento della richiesta */
#define WAIT_FOR_NAME_DIM 1
#define WAIT_FOR_NAME 2
#define WAIT_FOR_UDP_PORT 3
#define WAIT_FOR_COMMAND 4
#define WAIT_FOR_PARAM_DIM 5
#define WAIT_FOR_PARAM 6
#define WAIT_FOR_PLAYER_ACCEPT 7

/* Definizione del tipo booleano */
typedef enum {FALSE, TRUE} bool;

/* Definizione del tipo byte come intero su 8 bit */
typedef int8_t byte;

/* Definizione della struttura dati del giocatore.
	Sono mantenute tutte le informazioni di stato 
	necessarie alla corretta interazione. L'insieme
	dei client è implementato come una lista non
	ordinata a collegamento singolo */
typedef struct Player_t{
	byte nameSize;
	char *name;
	char *desiredName;
	byte rivalSize;
	char *rival;
	struct in_addr ip;
	short udpPort;
	byte toExecute;
	bool busy;
	short socket;
	byte status;
	int received;
	int remaining;
	struct Player_t *next;
} Player;

/* Funzione per la ricerca di un giocatore all'interno
	della lista in base al nome */
Player* getPlayerByName(Player *list, const char *name){
	Player *current = list;
	while(current != NULL){
		if(current->name != NULL){
			if(strcmp(current->name, name) == 0)
				break;
		}
		current = current->next;
	}
	return current;
}

/* Funzione per la ricerca di un giocatore all'interno
	della lista in base al socket su cui è connesso */
Player* getPlayerBySocket(Player *list, const int socket){
	Player *current = list;
	while(current != NULL && current->socket != socket){
		current = current->next;
	}
	return current;
}

/* Funzione per la rimozione di un giocatore
	dalla lista in base al socket su cui è connesso */
void removePlayerBySocket(Player **list, int socket){
	Player *current, *pre;
	current = *list;
	while(current != NULL){
		if(current->socket == socket){
			if(current == *list)
				*list = (*list)->next;
			else
				pre->next = current->next;
			return;
		}
		else{
			pre = current;
			current = current->next;
		}
	}
}

/* Definizione di una funzione wrapper per la printf()
	che esegue anche il flush dell'stdout */
void print(const char* format, ...){
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fflush(stdout);
}

/* Funzione per la conversione del parametro argv in porta */
long int stringToPort(const char *port){
	char *after;
	return strtol(port, &after, 10);
}

/* Funzione che effettua la terminazione di una partita tra due
	giocatori. Viene chiamata con il comando !DISCONNECT, oppure
	allo scadere del timeout */
void disconnect(Player *players, int socket){
	Player *p = getPlayerBySocket(players, socket);
	if(p->rival != NULL){
		/* Oltre a deallocare le strutture dati locali, invia all'avversario
			una notifica di avvenuta disconnessione, in modo che anche lui
			possa deallocare le proprie strutture dati */
		Player *r = getPlayerByName(players, p->rival);
		byte b = 0;
		send(r->socket, (void*)&b, sizeof(byte), 0);

		free(r->rival);
		r->rival = NULL;
		r->busy = FALSE;
		r->rivalSize = 0;
		r->status = WAIT_FOR_COMMAND;

		print("%s si è disconnesso da %s\n", p->name, r->name);
		print("%s è libero\n", p->name);
		print("%s è libero\n", r->name);
	}
	free(p->rival);
	p->rival = NULL;
	p->busy = FALSE;
	p->rivalSize = 0;
	p->status = WAIT_FOR_COMMAND;
}

/* Funzione che effettua l'aggiornamento delle strutture dati
	del server al termine di una partita. Viene chiamata
	quando uno dei due giocatori vince la partita e lo
	notifica al server. Non c'è necessità di avvisare nessuno
	dei due giocatori, perchè questi visualizzano l'esito della
	partita in base al confronto con la propria combinazione
	e all'esito inviato dall'avversario */
void gameOver(Player *players, int socket){
	Player *p = getPlayerBySocket(players, socket);
	if(p->rival != NULL){
		Player *r = getPlayerByName(players, p->rival);
		free(r->rival);
		r->rival = NULL;
		r->busy = FALSE;
		r->rivalSize = 0;
		r->status = WAIT_FOR_COMMAND;
		print("%s è libero\n", r->name);
	}
	free(p->rival);
	p->rival = NULL;
	p->busy = FALSE;
	p->rivalSize = 0;
	p->status = WAIT_FOR_COMMAND;

	print("%s è libero\n", p->name);
}

/* Funzione per la gestione degli errori nella funzione recv(). 
	Se il giocatore che ha provocato l'errore è coinvolto
	in un match, viene effettuata la disconnessione, dopodichè
	in ogni caso viene effettuata la rimozione del giocatore 
	dal server e la deallocazione delle sue strutture dati */
void handleReceiveError(Player **players, fd_set *aux_fds, int check, int socket){
	Player *p = getPlayerBySocket(*players, socket);
	if(p->rival != NULL)
		disconnect(*players, socket);
	if(check == 0){
		if(p->name != NULL)
			print("%s si è disconnesso\n", p->name);
		else
			print("Il giocatore connesso al socket %d si è disconnesso\n", socket);
	}
	else if(check < 0)
		print("Errore sul socket %d\n", p->socket);
	removePlayerBySocket(players, socket);
	free(p->name);
	free(p->desiredName);
	free(p);
	close(socket);
	FD_CLR(socket, aux_fds);
}

int main(int argc, char *argv[]){
	/* Dichiaro le variabili necessarie */
	int check;
	int newfd;
	int listener;
	struct sockaddr_in serveraddr;
	struct sockaddr_in clientaddr;
	int yes = 1;	
	socklen_t addrlen;
	
	fd_set aux_fds;
	fd_set read_fds;
	int fdmax;
	int desc;

	Player *players = NULL;

	int clientStringLength;
	char *clientStringList;

	if(argc != 3){
		print("Uso corretto: ./comb_server <host> <porta>\n");
		exit(1);
	}

	FD_ZERO(&aux_fds);
	FD_ZERO(&read_fds);

	/* Setup del socket di listening */
	if((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		perror("Non è stato possibile creare il cosket di ascolto");
		exit(1);
	}
	if(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
		perror("Non è stato possibile configurare il socket di ascolto");
		exit(1);
	}
	serveraddr.sin_family = AF_INET;
	if(inet_pton(AF_INET, argv[1], &serveraddr.sin_addr.s_addr) == 0){
		print("L'indirizzo fornito per il server non è valido\n");
		exit(1);
	}
	serveraddr.sin_port = htons(stringToPort(argv[2]));
	if(bind(listener, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) == -1){
		 perror("Non è stato possibile collegare l'indirizzo fornito al socket di ascolto");
		 exit(1);
	}		                                                                            
	if(listen(listener, 10) == -1){
		perror("Non è stato possibile attivare il socket di ascolto");
		exit(1);
	}

	print("Indirizzo: %s (Porta: %s)\n", argv[1], argv[2]);
		                                                                     
	FD_SET(listener, &aux_fds);

	fdmax = listener;

	while(1){                                                                           
		read_fds = aux_fds;                                                           
		if(select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1){     
			perror("Non è stato possibile eseguire la select");
			exit(1);
		}
		for(desc = 0; desc <= fdmax; desc++){ 
			if(FD_ISSET(desc, &read_fds)){ 
			 	if(desc == listener){
					/* Se ho ricevuto un input sul socket di listening, allora
						sto ricevendo una nuova richiesta di connessione. Aggiorno
						la lista dei descrittori e alloco le strutture dati per il nuovo client */
				 	addrlen = sizeof(clientaddr);
				  	if((newfd = accept(listener, (struct sockaddr *)&clientaddr, &addrlen)) == -1){
				     	perror("Non è stato possibile accettare una connessione in ingresso");
				    }
				    else{
				    	Player *p = calloc(1, sizeof(Player));
				    	p->ip = clientaddr.sin_addr;
				    	p->name = NULL;
				    	p->desiredName = NULL;
				    	p->rival = NULL;
				    	p->busy = TRUE;
				    	p->socket = newfd;
				    	p->next = NULL;
				    	p->received = 0;
				    	p->remaining = 0;
						p->next = players;
						players = p;
				    	p->status = WAIT_FOR_NAME_DIM;

				     	FD_SET(newfd, &aux_fds);
				     	if(newfd > fdmax){
					        fdmax = newfd;
				    	}
				    	print("Connessione stabilita con il client sul socket %d\n", newfd);
				    }
				}
				else{
					/* Se ho ricevuto un input da un altro socket, uno dei client connessi
						vuole comunicare. */
					Player *p = getPlayerBySocket(players, desc);
					Player *aux;
					switch(p->status){
						byte ack;
						byte nack;
						byte response;
						case WAIT_FOR_NAME_DIM:
							/* Il client è in questo stato se si è appena connesso e sta per 
								inviare la lunghezza del proprio nome. Una volta ricevuta, 
								viene allocato un buffer di appoggio per il nome scelto. */
							check = recv(desc, (void*)&p->nameSize, sizeof(byte), 0);
							if(check <= 0){
								handleReceiveError(&players, &aux_fds, check, desc);
								break;
							}
							p->desiredName = calloc(p->nameSize, sizeof(char));
							p->status = WAIT_FOR_NAME;
							p->received = 0;
							p->remaining = p->nameSize;		
							break;
						case WAIT_FOR_NAME:
							/* Dopo aver inviato la dimensione del nome, il client invia il nome
								scelto. Il server controlla che il nome non sia già utilizzato e
								lo comunica al client. Se il nome era già utilizzato, il client 
								dovrà inserirne uno diverso, altrimenti il nome è assegnato correttamente. */
							ack = 1;
							nack = 0;
							check = recv(desc, (void*)(p->desiredName + p->received), p->nameSize - p->received, 0);
							if(check <= 0){
								handleReceiveError(&players, &aux_fds, check, desc);
								break;
							}
							p->received += check;
							p->remaining -= check;
							if(p->remaining != 0){
								p->status = WAIT_FOR_NAME;
								break;
							}						
							aux = getPlayerByName(players, p->desiredName);
							if(aux == NULL){
								p->name = calloc(p->nameSize, sizeof(char));
								strncpy(p->name, p->desiredName, p->nameSize);
								send(desc, (void*)&ack, sizeof(byte), 0);
								p->status = WAIT_FOR_UDP_PORT;
								p->received = 0;
								p->remaining = 2;
							}
							else{
								free(p->desiredName);
								p->desiredName = NULL;
								p->nameSize = 0;
								p->received = 0;
								p->remaining = 0;
								send(desc, (void*)&nack, sizeof(byte), 0);
								p->status = WAIT_FOR_NAME_DIM;
							}						
							break;
						case WAIT_FOR_UDP_PORT:
							/* Dopo che il client ha scelto un nome valido, invia la sua porta
								UDP di ascolto, utilizzata per la comunicazione P2P. La connessione
								è completa e il client può iniziare a inviare comandi. */
							check = recv(desc, (void*)(&p->udpPort + p->received), sizeof(short) - p->received, 0);
							if(check <= 0){
								handleReceiveError(&players, &aux_fds, check, desc);
								break;
							}
							p->received += check;
							p->remaining -= check;
							if(p->remaining != 0){
								p->status = WAIT_FOR_UDP_PORT;
								break;
							}
							p->udpPort = ntohs(p->udpPort);
							p->received = 0;
							p->remaining = 0;
							print("%s si è connesso\n", p->name);
							print("%s è libero\n", p->name);
							p->busy = FALSE;
							p->status = WAIT_FOR_COMMAND;
							break;
						case WAIT_FOR_COMMAND:
							/* Dopo che il client si è connesso con successo al server, può iniziare
								a inviare comandi. I comandi sono codificati con un numero. */
							check = recv(desc, (void*)&p->toExecute, sizeof(byte), 0);
							if(check <= 0){
								handleReceiveError(&players, &aux_fds, check, desc);
								break;
							}
							switch(p->toExecute){
								case 1:		
									/*Comando WHO */
									/* Nel caso in cui il client abbia richiesto il comando WHO, il server
										costruisce la lista dei client attualmente connessi e la invia al client. */
									aux = players;
									clientStringLength = strlen("Client connessi al server: ");
									while(aux != NULL){
										if(aux->name != NULL){
											clientStringLength += strlen(aux->name);
											if(aux->busy == FALSE)
												clientStringLength += strlen("(libero)  ");
											else
												clientStringLength += strlen("(occupato)  ");
										}
										aux = aux->next;
									}
									clientStringList = (char*)calloc(clientStringLength, sizeof(char));
									clientStringLength = htonl(clientStringLength);
									send(desc, (void*)&clientStringLength, sizeof(int), 0);
									clientStringLength = ntohl(clientStringLength);
									aux = players;
									strncpy(clientStringList, "Client connessi al server: ", strlen("Client connessi al server: "));
									while(aux != NULL){
										if(aux->name != NULL){
											strncat(clientStringList, aux->name, strlen(aux->name));
											if(aux->next != NULL){
												if(aux->busy == FALSE)
													strncat(clientStringList, "(libero), ", strlen("(libero),"));
												else
													strncat(clientStringList, "(occupato), ", strlen("(occupato),"));
											}
											else{
												if(aux->busy == FALSE)
													strncat(clientStringList, "(libero)  ", strlen("(libero) "));
												else
													strncat(clientStringList, "(occupato)  ", strlen("(occupato) "));
											}
										}
										aux = aux->next;
									}
									clientStringList[clientStringLength - 1] = '\0';
									send(desc, (void*)clientStringList, clientStringLength, 0);
									free(clientStringList);
									clientStringList = NULL;
									clientStringLength = 0;
									p->received = 0;
									p->remaining = 0;
									p->status = WAIT_FOR_COMMAND;
									break;
								
								case 2:	
									/* Comando CONNECT */
									/* Nel caso in cui il client abbia richiesto di connettersi a un altro client,
										il server si mette in attesa della lunghezza del nome dell'avversario scelto */
									p->busy = TRUE;
									p->status = WAIT_FOR_PARAM_DIM;
									break;
								
								case 3:
									/* Comando DISCONNECT */
									/* Nel caso in cui il client abbia richiesto di disconnettersi da un avversario
										durante una partita, l'avversario viene informato e le strutture dati di
										entrambi i client sono aggiornate */
									disconnect(players, desc);
									break;
								case 4:
									/* Terminazione della partita */
									/* Nel caso in cui il client abbia notificato al server che una partita
										si è conclusa per vittoria, il server aggiorna le strutture dati di entrambi
										i client. */
									gameOver(players, desc);
							}
							p->toExecute = 0;
							break;					
						case WAIT_FOR_PARAM_DIM:
							/* Dopo che il client ha fatto richiesta al server di connettersi a un
								altro client, invia la dimensione del nome dell'avversario scelto.
								Il server alloca un buffer di dimensioni adeguate e si mette in
								attesa di ricevere il nome. */
							check = recv(desc, (void*)&p->rivalSize, sizeof(byte), 0);
							if(check <= 0){
								handleReceiveError(&players, &aux_fds, check, desc);
								break;
							}
							p->rival = calloc(p->rivalSize, sizeof(char));
							p->received = 0;
							p->remaining = p->rivalSize;
							p->status = WAIT_FOR_PARAM;
							break;						
						case WAIT_FOR_PARAM:
							/* Dopo che il client ha inviato la dimensione del nome dell'avversario, 
								invia il nome. Il server controlla che il giocatore scelto esista e che
								non sia occupato. Se il giocatore non esiste o è occupato, notifica
								lo sfidante e si rimette in attesa di un comando. Se invece l'avversario
								esiste, il server gli invia una richiesta di gioco con i dati dello
								sfidante e si mette in attesa di una risposta da parte dello sfidato */
							check = recv(desc, (void*)(p->rival + p->received), p->rivalSize - p->received, 0);
							if(check <= 0){
								handleReceiveError(&players, &aux_fds, check, desc);
								break;
							}
							p->received += check;
							p->remaining -= check;
							if(p->remaining != 0){
								p->status = WAIT_FOR_PARAM;
								break;
							}
							p->received = 0;
							aux = getPlayerByName(players, p->rival);
							if(aux == NULL){
								/*byte nack = 0;*/
								nack = 0;
								send(desc, (void*)&nack, sizeof(byte), 0);
								p->status = WAIT_FOR_COMMAND;
								p->busy = FALSE;
								free(p->rival);
								p->rival = NULL;
								p->rivalSize = 0;
							}
							else if(aux->busy == TRUE){
								/*byte nack = -1; */
								nack = -1;
								send(desc, (void*)&nack, sizeof(byte), 0);
								p->status = WAIT_FOR_COMMAND;
								p->busy = FALSE;
								free(p->rival);
								p->rival = NULL;
								p->rivalSize = 0;
							}
							else{
								if(aux->status == WAIT_FOR_COMMAND){
									byte req = -2;
									byte size = p->nameSize;
									aux->rival = calloc(p->nameSize, sizeof(char));
									strncpy(aux->rival, p->name, p->nameSize);
									send(aux->socket, (void*)&req, sizeof(byte), 0);
									send(aux->socket, (void*)&size, sizeof(byte), 0);
									send(aux->socket, (void*)p->name, size, 0);
									send(aux->socket, (void*)&p->ip, sizeof(struct in_addr), 0);
									p->udpPort = htons(p->udpPort);
									send(aux->socket, (void*)&p->udpPort, sizeof(short), 0);
									p->udpPort = ntohs(p->udpPort);
									p->status = WAIT_FOR_PLAYER_ACCEPT;
									aux->status = WAIT_FOR_PLAYER_ACCEPT;
									aux->busy = TRUE;
								}
							}
							break;
						case WAIT_FOR_PLAYER_ACCEPT:
							/* Dopo che il server ha inviato allo sfidato la richiesta di gioco
								si aspetta di ricevere da parte sua una risposta. Appena riceve la
								risposta la inoltra al client sfidante e aggiorna le strutture dati
								di conseguenza. Una volta iniziata la partita, il server si aspetta da
								entrambi i client coinvolti dei comandi. La partita è infatti gestita
								dai client in modo indipendente, e durante una partita essi possono 
								eseguire alcuni comandi, come da specifica.
								NOTA: in questa parte, Player *p è lo sfidato e Player *aux è lo sfidante */			
							aux = getPlayerByName(players, p->rival);
							check = recv(desc, (void*)&response, sizeof(byte), 0);
							if(check <= 0){
								handleReceiveError(&players, &aux_fds, check, desc);
								break;
							}
							send(aux->socket, (void*)&response, sizeof(byte), 0);
							if(response == -3){
								/* Lo sfidato ha rifiutato la partita */
								free(p->rival);
								p->rival = NULL;
								free(aux->rival);
								aux->rival = NULL;
								p->busy = FALSE;
								aux->busy = FALSE;
								p->status = WAIT_FOR_COMMAND;
								aux->status = WAIT_FOR_COMMAND;
							}
							else if(response == -4){
								/* Lo sfidato ha accettato la partita */
								aux->busy = TRUE;
								p->busy = TRUE;
								send(aux->socket, (void*)&p->ip, sizeof(struct in_addr), 0);
								p->udpPort = htons(p->udpPort);
								send(aux->socket, (void*)&p->udpPort, sizeof(short), 0);
								p->udpPort = ntohs(p->udpPort);
								aux->status = WAIT_FOR_COMMAND;
								p->status = WAIT_FOR_COMMAND;
								print("%s si è connesso a %s\n", aux->name, p->name);
							}
							break;
						default:
							break;
					}
				}
			}
		}
	}
	return 0;
}
