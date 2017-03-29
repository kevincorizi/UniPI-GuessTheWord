/*
Kevin Corizi, 501942
A.A. 2015/2016

Parte client dell'applicazione CombinazioneSegreta.
Il file non riporta errori o warning se compilato con
	gcc -Wall -pedantic -std=c89 comb_client.c -o comb_client
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

/* Definizione degli stati di operatività del client */
#define WAIT_FOR_INPUT 0
#define WAIT_FOR_SERVER_RESPONSE 1
#define WAIT_FOR_LIST_DIM 2
#define WAIT_FOR_LIST 3
#define WAIT_FOR_PLAYER_SIZE 4
#define WAIT_FOR_PLAYER_NAME 5
#define WAIT_FOR_PLAYER_IP 6
#define WAIT_FOR_PLAYER_PORT 7
#define WAIT_FOR_UDP_CONNECTION 8
#define WAIT_FOR_UDP_CONFIRM 9
#define WAIT_FOR_CODE 10
#define WAIT_FOR_ATTEMPT 11
#define WAIT_FOR_NOT_PLACE 12
#define WAIT_FOR_IN_PLACE 13

/* Definizione del tipo booleano */
typedef enum {FALSE, TRUE} bool;

/* Definizione del tipo byte come intero su 8 bit */
typedef int8_t byte;

/* Definizione del tipo Command, utilizzato per la corretta 
	gestione e archiviazione dei comandi */
typedef struct Command_t{
	char *command;
	char *param;
	char *description;
	bool inMatch;
	bool notInMatch;
} Command;

/* Dichiaro le variabili necessarie */
int check = 0;
int clientSocket = 0;
int udpSocket = 0;
struct sockaddr_in serverAddress;

struct sockaddr_in myAddress;
char *myName;
unsigned short myPort;	

struct sockaddr_in rivalAddress;
byte rivalNameSize;
char *rivalName;
struct in_addr rivalIP;
short rivalPort;

fd_set aux_fds;
fd_set read_fds;
int fdmax;
int desc;

byte status;
byte oldStatus;
int listLength = 0;
char *list;

char *command;
char *param;
byte cmd;

bool nameOK = FALSE;	

byte serverCode;

bool amSource = FALSE;

char myCode[4];
char rivalCode[4];

bool amPlaying = FALSE;
bool amWaiting = FALSE;

char attempt[5];
byte inPlace = 0;
byte notInPlace = 0;

int received = 0;
int remaining = 0;

struct timeval backupTimeout = {60, 0};
struct timeval timeout = {60, 0};

/* Definisco l'insieme dei comandi disponibili per l'utente */
Command commands[6] = {
	{"!help", "", "mostra l'elenco dei comandi disponibili", TRUE, TRUE},
	{"!who", "", "mostra l'elenco dei client connessi al server", TRUE, TRUE},
	{"!connect", " nome_client", "avvia una partita con l'utente nome_utente", FALSE, TRUE},
	{"!disconnect", "", "disconnette il client dall'attuale partita intrapresa con un altro peer", TRUE, FALSE},
	{"!combinazione", " comb", "permette al client di fare un tentativo con la combinazione comb", TRUE, FALSE},
	{"!quit", "", "disconnette il client dal server", TRUE, TRUE}
};

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

/* Funzione per il corretto inserimento della porta UDP di ascolto */
void getPort(unsigned short *s){
	char c;
	int check = scanf("%hu", s);
	do{
		c = getchar();
	} while(c != '\n');
	while(check != 1){
		print("La porta UDP non è valida!\n");
		print("Inserisci una porta UDP di ascolto valida: ");
		check = scanf("%hu", s);
		do{
			c = getchar();
		} while(c != '\n');
	}
}

/* Funzione per l'inserimento di una stringa da riga di comando */
void getString(char **string){
	char buffer[256];
	char c;
	memset(buffer, 0, 256);
	scanf(" %255[^\n]s", buffer);
	do{
		c = getchar();
	} while(c != '\n');
	*string = calloc(strlen(buffer) + 1, sizeof(char));
	strncpy(*string, buffer, strlen(buffer));
	(*string)[strlen(buffer)] = '\0';
}

/* Funzione per l'inserimento di un comando. La stringa inserita 
	viene divisa in modo da ottenere un comando e un eventuale 
	parametro da utilizzare per il suo svolgimento */
void getCommand(char **string, char **param){
	char *aux;
	char *token;
	getString(&aux);
	token = strtok(aux, " ");
	*string = calloc(strlen(token) + 1, sizeof(char));
	strncpy(*string, token, strlen(token));
	(*string)[strlen(token)] = '\0';
	token = strtok(NULL, " ");
	if(token != NULL){
		*param = calloc(strlen(token) + 1, sizeof(char));
		strncpy(*param, token, strlen(token));
		(*param)[strlen(token)] = '\0';
	}
	else
		*param = NULL;
	free(aux);
}

/* Funzione per l'inserimento di un solo carattere da riga di comando */
char getChar(){
	char c;
	scanf(" %c", &c);
	while(getchar() != '\n'){
		print("Devi inserire un solo carattere\n");
		do{
			c = getchar();
		} while(c != '\n');
		scanf(" %c", &c);
	}
	return c;
}

/* Funzione che visualizza l'insieme dei comandi disponibili */
/* Richiamata dal comando !HELP */
void printMenu(){
	int i;
	print("Sono disponibili i seguenti comandi:\n");
	for (i = 0; i < 6; ++i){
		if((amPlaying == TRUE && commands[i].inMatch == TRUE) || (amPlaying == FALSE && commands[i].notInMatch == TRUE))
			print(" * %s%s  -->  %s\n", commands[i].command, commands[i].param, commands[i].description);
	}
}

/* Funzione per il controllo della combinazione */
/* Accetta solo combinazioni di quattro lettere minuscole */
bool isCode(char *code){
	bool check = FALSE;
	int loop;
	if(strlen(code) == 4){
		for(loop = 0; loop < 4; loop++){
			if(code[loop] < 'a' || code[loop] > 'z')
				break;
		}
		if(loop == 4)
			check = TRUE;
	}
	return check;
}

/* Funzione per l'inserimento di una combinazione valida */
void getClientCode(){
	char *tmp;
	bool check = FALSE;
	do{
		getString(&tmp);
		check = isCode(tmp);
		if(check == FALSE)
			print("Scegli un codice di 4 lettere minuscole: ");
	}
	while(check == FALSE);
	strncpy(myCode, tmp, 4);
	free(tmp);
}

/* Funzione che inizializza la connessione UDP tra due peer */
void buildUDPSocket(int *sock){
	*sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(*sock == -1){
		print("Si è verificato un errore nella connessione a %s!\n", rivalName);
		exit(1);
	}
	memset(&myAddress, 0, sizeof(myAddress));
	myAddress.sin_family = AF_INET;
	myAddress.sin_port = htons(myPort);
	myAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(*sock, (struct sockaddr*)&myAddress, sizeof(myAddress)) == -1){
		print("Errore nella bind sul socket UDP\n");
		exit(1);
	}
	FD_SET(*sock, &aux_fds);
	if(fdmax < *sock)
		fdmax = *sock;

	memset(&rivalAddress, 0, sizeof(rivalAddress));
	rivalAddress.sin_family = AF_INET;
	rivalAddress.sin_port = htons(rivalPort);
	rivalAddress.sin_addr.s_addr = rivalIP.s_addr;
	if(connect(*sock, (struct sockaddr*)&rivalAddress, sizeof(rivalAddress)) == -1){
		print("Errore nella connect UDP\n");
		exit(1);
	}
}

/* Funzione per la stampa del simbolo di shell */
void printGlyph(){
	if(amPlaying == FALSE)
		print("\n> ");
	else
		print("\n# ");
}

/* Funzione per la gestione della disconnessione da un peer */
/* Dealloca le strutture dati locali e chiude la comunicazione UDP */
void handleDisconnection(int *sock){	
	if(rivalName != NULL){
		free(rivalName);
		rivalName = NULL;
	}
	rivalPort = 0;
	rivalNameSize = 0;	
	memset(&rivalAddress, 0, sizeof(rivalAddress));
	memset(&rivalIP, 0, sizeof(rivalIP));
	if(*sock != 0){
		close(*sock);
		FD_CLR(*sock, &aux_fds);
		FD_CLR(*sock, &read_fds);
		if(*sock > clientSocket)
			fdmax = clientSocket;
		*sock = 0;
	}
	status = WAIT_FOR_INPUT;
	amPlaying = FALSE;
	amWaiting = FALSE;
	amSource = FALSE;
}

/* Funzione per la gestione della disconnessione del giocatore */
void handleMyDisconnection(int *sock){
	print("Disconnessione avvenuta con successo, ti sei arreso.\n");
	handleDisconnection(sock);
	printGlyph();
}

/* Funzione per la gestione della disconnessione dell'avversario */
void handleRivalDisconnection(int *sock){
	print("%s si è disconnesso, hai vinto!\n", rivalName);
	handleDisconnection(sock);
	printGlyph();
}

int main(int argc, char const *argv[]){
	if(argc != 3){
		print("Uso corretto: ./comb_client <host remoto> <porta>\n");
		exit(1);
	}

	/* Connessione al server TCP */
	clientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(clientSocket == -1){
		print("Non è stato possibile creare il socket\n");
		exit(1);
	}
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(stringToPort(argv[2]));
	check = inet_pton(AF_INET, argv[1], &serverAddress.sin_addr.s_addr);
	if(check == 0){
		print("L'indirizzo fornito per il server non è valido\n");
		exit(1);
	}
	check = connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
	if(check == -1){
		print("Non è stato possibile connettersi al server\n", clientSocket);
		exit(1);
	}

	print("\nConnessione al server %s (porta %s) effettuata con successo\n", argv[1], argv[2]);

	FD_ZERO(&aux_fds);
	FD_ZERO(&read_fds);
	FD_SET(0, &aux_fds);
	FD_SET(clientSocket, &aux_fds);

	fdmax = clientSocket;

	status = WAIT_FOR_INPUT;

	/* Inserimento dei dati del giocatore */
	do{
		byte nameLen;
		print("\nInserisci il tuo nome: ");
		getString(&myName);
		if(strlen(myName) > 255){
			print("Inserisci un nome più breve: ");
			continue;
		}
		nameLen = strlen(myName) + 1;
		send(clientSocket, (void*)&nameLen, sizeof(byte), 0);
		send(clientSocket, (void*)myName, nameLen, 0);
		recv(clientSocket, (void*)&nameOK, sizeof(bool), 0);
		if(nameOK == FALSE){
			print("Il nome che hai scelto non è disponibile!\n");
			free(myName);
			myName = NULL;
		}
		status = WAIT_FOR_INPUT;
	}
	while(nameOK == FALSE);

	print("Inserisci la porta UDP di ascolto: ");
	getPort(&myPort);
	myPort = htons(myPort);
	while(myPort == serverAddress.sin_port || ntohs(myPort) < 1024){
		print("La porta UDP non è valida!\n");
		print("Inserisci una porta UDP di ascolto valida: ");
		getPort(&myPort);
		myPort = htons(myPort);
	}
	send(clientSocket, (void*)&myPort, sizeof(short), 0);
	myPort = ntohs(myPort);

	status = WAIT_FOR_INPUT;
	oldStatus = WAIT_FOR_INPUT;

	printMenu();

	printGlyph();

	while(1){
		read_fds = aux_fds;
		/* Per la corretta gestione del tempo di inattività, alla 
			SELECT è passato come parametro un timeout 
			solo se il giocatore è attualmente coinvolto in una partita */
		if(amPlaying)
			check = select(fdmax + 1, &read_fds, NULL, NULL, &timeout);
		else
			check = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
		if(check == -1){
			perror("Non è stato possibile eseguire la select");
			exit(1);
		}
		if(check == 0){
			/* In caso di scadenza del timeout, il giocatore dealloca 
				le sue strutture locali e informa il server dell'avvenuta 
				disconnessione. Il server informerà l'avversario. */
			byte c = 3;
			print("Partita interrotta per inattività\n");
			handleDisconnection(&udpSocket);
			timeout = backupTimeout;
			send(clientSocket, (void*)&c, sizeof(byte), 0);
			printGlyph();
			continue;
		}
		for(desc = 0; desc <= fdmax; desc++){
			if(FD_ISSET(desc, &read_fds)){
				if(desc == 0){
					/* Se ho ricevuto input dalla tastiera, resetto il 
						timer di timeout */
					timeout = backupTimeout;
					switch(status){
						case WAIT_FOR_CODE:
							/* All'inizio di una partita viene richiesto di 
								inserire la propria combinazione */
							getClientCode();
							if(amSource == TRUE){
								/* Se il giocatore ha iniziato il match, gioca 
									per primo */
								print("E' il tuo turno");
								printGlyph();
								status = WAIT_FOR_INPUT;
							}
							else{
								/* Se il giocatore ha ricevuto una richiesta, si 
									mette in attesa di un tentativo da parte 
									dell'avversario */
								print("E' il turno di %s\n", rivalName);
								printGlyph();
								status = WAIT_FOR_ATTEMPT;
							}
							break;
						default:
							getCommand(&command, &param);
							cmd = 0;
							while(cmd < 6 && strcmp(command, commands[cmd].command) != 0)
								cmd++;
							free(command);
							if(amWaiting == TRUE){
								print("Attendi la risposta di %s\n", rivalName);
								if(param != NULL)
									free(param);
								continue;
							}
							if(status == WAIT_FOR_ATTEMPT && cmd == 4){
								/* Un giocatore può eseguire un solo tentativo 
									alla volta */
								print("E' il turno di %s", rivalName);
								if(param != NULL)
									free(param);
								printGlyph();
								continue;
							}
							if(cmd > 5){
								print("Il comando selezionato non è valido");
								if(param != NULL)
									free(param);
								printGlyph();
								continue;
							}
							if(commands[cmd].notInMatch == FALSE && amPlaying == FALSE){
								print("Non puoi usare questo comando fuori da una partita");
								if(param != NULL)
									free(param);
								printGlyph();
								continue;
							}
							if(commands[cmd].inMatch == FALSE && amPlaying == TRUE){
								print("Non puoi usare questo comando durante una partita");
								if(param != NULL)
									free(param);
								printGlyph();
								continue;
							}
							switch(cmd){
								case 0:
									/* Comando !HELP */
									printMenu();
									printGlyph();
									/* Se digito il comando !HELP durante una partita, 
										resto in uno stato di gioco e non torno all'attesa di 
										comandi */
									if(!amPlaying)
										status = WAIT_FOR_INPUT;
									break;
								case 1:
									/* Comando !WHO */
									send(clientSocket, (void*)&cmd, sizeof(byte), 0);
									received = 0;
									remaining = sizeof(int);
									/* Se sono attualmente coinvolto in una partita, salvo lo stato 
										attuale per poter riprendere dopo l'esecuzione del comando !WHO */
									if(amPlaying)
										oldStatus = status;
									status = WAIT_FOR_LIST_DIM;
									break;
								case 2:
									/* Comando !CONNECT */
									if(param != NULL && strcmp(param, "") != 0){
										if(strcmp(param, myName) == 0){
											print("Non puoi iniziare una partita con te stesso");
											printGlyph();
										}
										else{
											/* Se il giocatore a cui mi voglio connettere è valido, invio
												una richiesta al server e attendo di sapere se posso
												iniziare una partita */
											byte paramLength = strlen(param) + 1;
											send(clientSocket, (void*)&cmd, sizeof(byte), 0);
											send(clientSocket, (void*)&paramLength, sizeof(byte), 0);
											send(clientSocket, (void*)param, paramLength, 0);
											status = WAIT_FOR_SERVER_RESPONSE;
											amWaiting = TRUE;
											amSource = TRUE;
											rivalName = calloc(strlen(param) + 1, sizeof(char));
											strncpy(rivalName, param, strlen(param) + 1);
											rivalName[strlen(param)] = '\0';
										}
										free(param);
									}
									else{
										print("Devi specificare un giocatore");
										printGlyph();
									}
									break;
								case 3:
									/* Comando !DISCONNECT */
									/* Dealloco le variabili di gioco locali e informo il server, che 
										provvederà a informare il mio avversario attuale */
									send(clientSocket, (void*)&cmd, sizeof(byte), 0);
									handleMyDisconnection(&udpSocket);
									break;
								case 4:
									/* Comando !COMBINAZIONE */
									if(isCode(param)){
										/* Se il mio tentativo è valido, lo invio al mio avversario e attendo
											che mi comunichi l'esito */
										char tmp[5];
										strncpy(tmp, param, 4);
										tmp[4] = '\0';
										send(udpSocket, &tmp, 5 * sizeof(char), 0);
										status = WAIT_FOR_NOT_PLACE;
									}
									else{
										print("Il tentativo deve essere di 4 lettere minuscole");
										printGlyph();
										status = WAIT_FOR_INPUT;
									}
									free(param);
									break;
								case 5:
									/* Comando !QUIT */
									/* Dealloco le variabili locali e chiudo la connessione con il server TCP,
										che riceverà la notifica dell'avvenuta uscita e aggiornerà le sue variabili */
									if(rivalName != NULL){
										free(rivalName);
										rivalName = NULL;
									}
									free(myName);
									FD_CLR(clientSocket, &aux_fds);
									close(clientSocket);
									print("Client disconnesso correttamente\n");
									return 0;
								default:
									break;
							}
							break;
					}
				}
				else if(desc == clientSocket){
					/* Se ho ricevuto input dalla tastiera */
					switch(status){
						case WAIT_FOR_SERVER_RESPONSE:
							/* Il giocatore si pone in questo stato quando ha fatto una richiesta di gioco
								e deve sapere se il giocatore indicato è valido */
							check = recv(clientSocket, (void*)&serverCode, sizeof(byte), 0);
							if(check <= 0){
								print("C'è stato un errore con il server\n");
								exit(1);
							}
							switch(serverCode){
								case 0:
									/* Il giocatore specificato non esiste */
									print("%s non esiste", rivalName);
									printGlyph();
									free(rivalName);
									rivalName = NULL;
									status = WAIT_FOR_INPUT;
									amPlaying = FALSE;
									amWaiting = FALSE;
									break;
								case -1:
									/* Il giocatore indicato è già impegnato in un'altra partita */
									print("%s è impegnato in un'altra partita", rivalName);
									printGlyph();
									free(rivalName);
									rivalName = NULL;
									status = WAIT_FOR_INPUT;
									amPlaying = FALSE;
									amWaiting = FALSE;
									break;
								case -3:
									/* Il giocatore indicato ha rifiutato la partita */
									print("%s ha rifiutato la partita", rivalName);
									printGlyph();
									free(rivalName);
									rivalName = NULL;
									status = WAIT_FOR_INPUT;
									amPlaying = FALSE;
									amWaiting = FALSE;
									amSource = FALSE;
									break;
								case -4:
									/* Il giocatore indicato ha accettato la partita */
									print("%s ha accettato la partita\n", rivalName);
									status = WAIT_FOR_PLAYER_IP;
									received = 0;
									remaining = sizeof(struct in_addr);
									amWaiting = TRUE;
									break;
								default:
									break;
							}
							break;
						case WAIT_FOR_LIST_DIM:
							/* Dopo l'esecuzione del comando !WHO, attendo che il server invii la lunghezza
								della lista dei client connessi */
							check = recv(clientSocket, (void*)(&listLength + received), sizeof(int) - received, 0);
							if(check <= 0){
								print("C'è stato un errore con il server.\n");
								exit(1);
							}
							received += check;
							remaining -= check;
							if(remaining != 0){
								status = WAIT_FOR_LIST_DIM;
								break;
							}
							listLength = ntohl(listLength);
							list = calloc(listLength, sizeof(char));
							received = 0;
							remaining = listLength;
							status = WAIT_FOR_LIST;
							break;
						case WAIT_FOR_LIST:
							/* Dopo aver ricevuto la lunghezza della lista e dopo aver allocato
								un buffer di dimensioni adeguate, ricevo la lista dei client, la stampo
								e successivamente la dealloco */
							check = recv(clientSocket, (void*)(list + received), listLength - received, 0);
							if(check <= 0){
								print("C'è stato un errore con il server.\n");
								exit(1);
							}
							received += check;
							remaining -= check;
							if(remaining != 0){
								status = WAIT_FOR_LIST;
								break;
							}
							received = 0;
							print("%s", list);
							free(list);
							list = NULL;
							listLength = 0;
							printGlyph();
							/* Se avevo digitato il comando !WHO durante una partita, ripristino lo stato di gioco */
							if(amPlaying)
								status = oldStatus;
							else
								status = WAIT_FOR_INPUT;
							break;
						case WAIT_FOR_PLAYER_SIZE:
							/* Se sono stato sfidato da un avversario, oppure una mia richiesta di gioco è
								stata accettata, attendo di ricevere dal server i dati relativi all'avversario */
							check = recv(clientSocket, (void*)&rivalNameSize, sizeof(byte), 0);
							if(check <= 0){
								print("C'è stato un errore con il server.\n");
								exit(1);
							}
							rivalName = calloc(rivalNameSize, sizeof(char));
							received = 0;
							remaining = rivalNameSize;
							status = WAIT_FOR_PLAYER_NAME;
							break;
						case WAIT_FOR_PLAYER_NAME:
							/* Dopo aver ricevuto la dimensione del nome dell'avversario e allocato un buffer
								di dimensioni adeguate, ricevo il nome */
							check = recv(clientSocket, (void*)(rivalName + received), rivalNameSize - received, 0);
							if(check <= 0){
								print("C'è stato un errore con il server.\n");
								exit(1);
							}
							received += check;
							remaining -= check;
							if(remaining != 0){
								status = WAIT_FOR_PLAYER_NAME;
								break;
							}
							received = 0;
							remaining = sizeof(struct in_addr);
							status = WAIT_FOR_PLAYER_IP;
							break;
						case WAIT_FOR_PLAYER_IP:
							/* Dopo aver ricevuto il nome dell'avversario, ricevo dal server il suo indirizzo IP */
							check = recv(clientSocket, (void*)(&rivalIP + received), sizeof(struct in_addr) - received, 0);
							if(check <= 0){
								print("C'è stato un errore con il server.\n");
								exit(1);
							}							
							received += check;
							remaining -= check;
							if(remaining != 0){
								status = WAIT_FOR_PLAYER_IP;
								break;
							}
							received = 0;
							remaining = sizeof(short);
							status = WAIT_FOR_PLAYER_PORT;
							break;
						case WAIT_FOR_PLAYER_PORT:
							/* Dopo aver ricevuto l'indirizzo IP dell'avversario, ricevo dal server la sua porta UDP di ascolto */
							check = recv(clientSocket, (void*)(&rivalPort + received), sizeof(short) - received, 0);	
							if(check <= 0){
								print("C'è stato un errore con il server.\n");
								exit(1);
							}
							received += check;
							remaining -= check;
							if(remaining != 0){
								status = WAIT_FOR_PLAYER_PORT;
								break;
							}	
							received = 0;
							rivalPort = ntohs(rivalPort);
							/* Dopo aver ricevuto tutti i dati dell'avversario posso aprire la connessione UDP */ 
							if(amSource == TRUE){
								/* Se il giocatore ha effettuato la richiesta, dopo che l'avversario ha accettato e 
									dopo aver ricevuto i suoi dati, il giocatore costruisce il socket UDP, invia
									un ack all'avversario e si mette in attesa di ricevere un ack */
								byte i = 1;
								buildUDPSocket(&udpSocket);
								send(udpSocket, &i, sizeof(byte), 0);
								status = WAIT_FOR_UDP_CONFIRM;
								amWaiting = TRUE;
								amPlaying = FALSE;
								break;
							}
							else{
								/* Se il giocatore ha ricevuto una richiesta da un avversario di cui ha appena ottenuto 
									i dati, sceglie se accettare o meno l'invito */
								char response;
								print("%s ti ha invitato a giocare, accetti? (S/N) ", rivalName);
								do{
									response = getChar();
									if(response != 'S' && response != 'N'){
										print("\nRispondi: accetti (S) o rifiuti (N) la partita? ");
									}
								}
								while(response != 'S' && response != 'N');
								if(response == 'S'){
									/* Se il giocatore accetta la sfida, invia al server una notifica, costruisce 
										il socket UDP e si mette in attesa di un messaggio da parte dello sfidante
										Notare che questa operazione è sempre eseguita prima dell'invio dell'ack
										da parte dello sfidante */
									byte ack = -4;
									send(clientSocket, (void*)&ack, sizeof(byte), 0);
									buildUDPSocket(&udpSocket);
									amWaiting = TRUE;
									amPlaying = FALSE;
									status = WAIT_FOR_UDP_CONNECTION;
								}
								else if(response == 'N'){
									/* Se il giocatore rifiuta la sfida, invia al server una notifica, dealloca
										le strutture dati locali relative all'avversario e si porta nello stato iniziale */
									byte nack = -3;
									send(clientSocket, (void*)&nack, sizeof(byte), 0);
									printGlyph();
									free(rivalName);
									rivalName = NULL;
									rivalPort = 0;
									rivalNameSize = 0;	
									memset(&rivalAddress, 0, sizeof(rivalAddress));
									memset(&rivalIP, 0, sizeof(rivalIP));
									status = WAIT_FOR_INPUT;
									amPlaying = FALSE;
									amWaiting = FALSE;
									amSource = FALSE;
								}
								break;
							}
							break;
						default:
							/* In questo stato vengono ricevute tutte le comunicazioni dal server non
								gestite in nessuno dei precedenti stati. Queste comunicazioni si
								riducono alla ricezione delle notifiche di disconnessione dell'avversario
								mentre il giocatore è coinvolto nella partita, o alla ricezione
								di un invito a giocare */
							amSource = FALSE;
							check = recv(clientSocket, (void*)&serverCode, sizeof(byte), 0);
							if(check <= 0){
								print("C'è stato un errore con il server.\n");
								exit(1);
							}
							switch(serverCode){
								case -2:
									/* Sono stato invitato a giocare */
									status = WAIT_FOR_PLAYER_SIZE;
									break;
								case 0:
									/* Il mio avversario si è disconnesso */
									handleRivalDisconnection(&udpSocket);
									break;
								default:
									break;
							}
					}
				}
				else{
					/* Se ho ricevuto input dal socket UDP, resetto il timer di timeout */
					byte i;
					byte j;
					byte in;
					byte out;
					timeout = backupTimeout;
					switch(status){
						case WAIT_FOR_UDP_CONNECTION:
							/* Se il giocatore ha accettato una sfida da parte di un avversario,
								si mette in attesa dell'ack */
							check = recv(udpSocket, &i, sizeof(byte), 0);
							if(check <= 0){
								handleRivalDisconnection(&udpSocket);
								break;
							}
							if(i == 1){
								/* Una volta ricevuto l'ack, lo rimanda indietro allo sfidante, 
									completando così l'handshake e dando inizio alla sfida. */
								send(udpSocket, &i, sizeof(byte), 0);
								print("Partita con %s avviata!\n", rivalName);
								status = WAIT_FOR_CODE;
								amPlaying = TRUE;
								amWaiting = FALSE;
								print("Digita la tua combinazione segreta: ");
							}
							break;
						case WAIT_FOR_UDP_CONFIRM:
							/* Lo sfidante, dopo aver inviato un ack allo sfidato, si mette in attesa
								di ricevere da parte sua un ulteriore ack. Quando lo riceve,
								l'handshake si conclude e la partita ha inizio */
							check = recv(udpSocket, &i, sizeof(byte), 0);
							if(check <= 0){
								handleRivalDisconnection(&udpSocket);
								break;
							}
							if(i == 1){
								status = WAIT_FOR_CODE;
								print("Partita con %s avviata!\n", rivalName);
								amPlaying = TRUE;
								amWaiting = FALSE;
								print("Digita la tua combinazione segreta: ");
							}
							break;
						case WAIT_FOR_ATTEMPT:
							/* I giocatori coinvolti in una sfida sono in questo stato quando è il turno
								del rispettivo avversario. Una volta ricevuto un tentativo, il giocatore
								effettua il confronto con la propria combinazione e comunica all'avversario
								l'esito del tentativo */
							in = 0;
							out = 0;
							check = recv(udpSocket, attempt, 5 * sizeof(char), 0);
							if(check <= 0){
								handleRivalDisconnection(&udpSocket);
								break;
							}
							attempt[4] = '\0';
							if(strcmp(attempt, myCode) == 0){
								/* Se il tentativo dell'avversario è esatto, il giocatore oltre a inviare
									l'esito all'avversario dealloca le proprie strutture relative
									alla sfida, che ormai è conclusa */
								print("%s dice %s, %s vince la partita", rivalName, attempt, rivalName);
								in = 4;
								out = 0;
								send(udpSocket, &out, sizeof(byte), 0);
								send(udpSocket, &in, sizeof(byte), 0);
								handleDisconnection(&udpSocket);
								printGlyph();
							}
							else{
								char codeCopy[4];
								strncpy(codeCopy, myCode, 4);
								print("Appoggio = %s\n", codeCopy);
								i = 0;
								j = 0;
								print("%s dice %s, il suo tentativo è sbagliato\n", rivalName, attempt);
								for(i = 0; i < 4; i++){
									for(j = 0; j < 4; j++){
										if(attempt[j] == codeCopy[i]){
											if(i == j)
												in++;
											else
												out++;
											codeCopy[i] = '0';
										}
									}
								}
								send(udpSocket, &out, sizeof(byte), 0);
								send(udpSocket, &in, sizeof(byte), 0);
								print("E' il tuo turno");
								printGlyph();
								status = WAIT_FOR_INPUT;
							}
							break;
						case WAIT_FOR_NOT_PLACE:
							/* I giocatori coinvolti in una sfida sono in questo stato dopo aver effettuato
								un tentativo. */
							check = recv(udpSocket, &notInPlace, sizeof(byte), 0);
							if(check <= 0){
								handleRivalDisconnection(&udpSocket);
								break;
							}
							status = WAIT_FOR_IN_PLACE;
							break;
						case WAIT_FOR_IN_PLACE:
							/* I giocatori coinvolti in una sfida sono in questo stato dopo aver effettuato
								un tentativo. Dopo aver ricevuto il numero di lettere giuste al posto
								sbagliato, ricevono il numero di lettere giuste al posto giusto.
								Se quest'ultimo valore vale quattro, allora il tentativo del giocatore
								era esatto. Il giocatore quindi dealloca le proprie strutture dati relative
								alla sfida e invia al server la notifica di avvenuto termine della partita.
								Il server provvederà ad aggiornare le strutture dati relative ai client. */
							check = recv(udpSocket, &inPlace, sizeof(byte), 0);
							if(check <= 0){
								handleRivalDisconnection(&udpSocket);
								break;
							}
							if(inPlace == 4){
								handleDisconnection(&udpSocket);
								print("Congratulazioni, hai vinto!");
								send(clientSocket, (void*)&inPlace, sizeof(inPlace), 0);
								printGlyph();
							}
							else{
								print("%s dice: %d lettere giuste al posto giusto, %d lettere giuste al posto sbagliato\n", rivalName, inPlace, notInPlace);
								print("E' il turno di %s", rivalName);
								printGlyph();
								status = WAIT_FOR_ATTEMPT;
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
