//------------------------------------------------------------//
/*---------------------MASTERMIND CLIENT---------------------*/
//------------------------------------------------------------//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>		
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <regex.h>
#include <errno.h>
#include <sys/un.h>


#define MAX_USERS 256
#define MAX_BUFFER 512

#define LOGIN_CODE 'l'
#define PORT_CODE 'p'
#define WHO_CODE 'w'
#define RESPP_CODE 'r'
#define OK_CODE 'y'
#define NO_CODE 'n'
#define PLAY_CODE 'g'
#define PARAM_CODE 'h'
#define DISC_CODE 'd'
#define CRASH_CODE 'x'
#define COMB_CODE 'c'
#define OUTCOME_CODE 'o'
#define ATTACK_CODE 'a'
#define EXIT_CODE 'e'

char opp_ip[MAX_BUFFER];	//formato presentazione dell'IP 

char nickname[MAX_BUFFER];	//stringa per il nome utente
char opp_nickname[MAX_BUFFER];

char UDP_port[MAX_BUFFER];	//stringa porta UDP passata dall'utente
char opp_UDP_port[MAX_BUFFER];

int busy;		//indica se l'utente è occupato in una partita con un'altro utente
int play=0;		//indica se l'utente sta giocando con un altro utente (è già stata scelta
				//la comb)
int attempt=0;	//permette a entrambi i giocatori di provare una comb in uno stesso round
				//(ammetto il pareggio)
int block=0;	//impedisce che a un'utente vengano proposte nuove sfide se sta attendendo una 
				//risposta da un giocatore sfidato
char sec_comb[5];	//comb dell'utente
char att_comb[5];	//comb d'attacco dell'utente
	                 
char right;	//elementi giusti in posizione giusta
char wrong;	//elementi giusti ma in posizione sbagliata

char turn;	//indica quale giocatore deve provare a indovinare la comb

struct sockaddr_in server_par; 	//parametri del server
struct sockaddr_in my_par; 	//miei parametri
struct sockaddr_in opp_par;	//parametri del mio sfidante

int server_sk;	//socket di comunicazione con il server
int UDP_sk;	//socket di comunicazione con il mio sfidante

char kb_buf[MAX_BUFFER];	//buffer per gli inserimenti da tastiera
char TCP_buf_in[MAX_BUFFER];	//buffer TCP di ingresso
char TCP_buf_out[MAX_BUFFER];	//buffer TCP di uscita
char UDP_buf_in[MAX_BUFFER];	//buffer UDP di ingresso
char UDP_buf_out[MAX_BUFFER];	//buffer UDP di uscita

struct timeval time;	//per la select e per il timer

fd_set master;		//insieme dei descrittori di socket
fd_set read_fds;	//array usato durante la select() per non modificare master
int max_des;		//numero massimo di descrittori che possono essere controllati con la select
int max_UDP_des;	//numero massimo di descrittori che possono essere controllati con la select (UDP)

int ret;
int port, test;
int last=0;
int i, c=0;
void play_resp();


//funzioni per invio/ricezione pacchetti
int TCP_send() 
{
	int howmany;
	howmany = send(server_sk, (void*)TCP_buf_out, MAX_BUFFER, 0);
	if(howmany < MAX_BUFFER)
	{
    		printf("\nTRASMISSIONE INCOMPLETA\n");
    		return -1;
  	}
  	return howmany;
}

int TCP_recv()
{					
	int in;
	if((in = recv(server_sk, (void*)TCP_buf_in, MAX_BUFFER, 0))<MAX_BUFFER)
	{
		return -1;
	}
	return in;
}

int UDP_send()
{
	int howmany;
	howmany = sendto(UDP_sk, (void*)UDP_buf_out, MAX_BUFFER, 0, (struct sockaddr*)&opp_par, sizeof(opp_par));
	if(howmany < MAX_BUFFER)
  	{
    		printf("TRASMISSIONE INCOMPLETA\n");
    		return -1;
  	}
  	return howmany;
}

int UDP_recv()
{					
	int in;
	socklen_t len = sizeof(opp_par);
	if((in = recvfrom(UDP_sk, (void*)UDP_buf_in,MAX_BUFFER,0,(struct sockaddr*)&opp_par, &len))<MAX_BUFFER)
    	{
		return -1;
	}
	return in;
}

void kb_read()
{
	memset((void*)kb_buf, 0, MAX_BUFFER);
	for(;;)
	{
		kb_buf[c]=getchar();
		if(kb_buf[c]=='\n')	//è stato premuto invio
		{
			kb_buf[c]='\0';
			c=0;
			break;
		}
		else
		{
			c++;
		}
    }
	c=0;
}


//funzioni di utilità
void copy(char* elem1, char* elem2, char sym)
{
	int i=0;
	while(elem2[i]!= sym)
	{
		elem1[i]=elem2[i];
		i++;
	}
	elem1[i]='\0';
  	last=i;		//indice fino a cui ho copiato, mi serve in param_resp()
}

//verifica che nella comb non siano presenti nè caratteri letterali nè simboli: solo numeri da 0 a 9
int check_comb(char* elem)
{
	int app;
	int i=0;
	while(1)
	{
		if(i<5)
		{
			app = elem[i] - '0';	//trasformo il carattere in numero
			if(elem[i]=='\0')
			{
				if(i==4)
				{
				
					return 0;
				}
				else
				{
					printf("comb troppo corta\n");
					return -1;
				}
			}
			else
			{
				if(app>=0 && app<=9)
				{
					i++;
				}
				else
				{
					printf("Ci sono delle lettere nella comb\n");
					return -1;
				}
			}
		}
		else
		{
			printf("comb troppo lunga\n");
			return -1;
		}
	}
}

void wait()
{
	printf("Richiesta a %s inoltrata: si prega di attendere...\n", opp_nickname);
	return;
}

void timer()
{
	time.tv_sec=60;		//scatta dopo un minuto di inattività
	time.tv_usec=0;
}

//cerca nella comb inviata dal mio avversario gli elementi giusti in posizione giusta
int find_right(char* attack_comb, char* my_comb)
{
	int i=0, right=0;
	while(attack_comb[i]!='\0')
	{
		if(attack_comb[i]==my_comb[i])
		{
			right=right+1;
			my_comb[i]='.';		//segnalo il carattere come già controllato
						//mi serve per find_wrong()
			attack_comb[i]='.';
		}
		i++;
	}
	return right;
}

//cerca nella comb inviata dal mio avversario gli elementi giusti ma in posizione sbagliata
int find_wrong(char* attack_comb, char* my_comb)
{
	int i, j, wrong=0;
	for(i=0; i<4; i++)	//attack_comb
	{
		if(attack_comb[i]!='.')	//già controllato
		{
			for(j=0; j<4; j++)	//my_comb
			{
				if(attack_comb[i]==my_comb[j])
				{
					wrong=wrong+1;
					attack_comb[i]='.';
					my_comb[j]='.';
					break;
				}
			}
		}
	}
	return wrong;
}

int test_IP(char* ip) //verifica la validita' dell'IP passato dall'utente
{
	struct sockaddr_in try;
	int res;
	res = inet_pton(AF_INET, ip, &(try.sin_addr.s_addr));
	if(res==-1)
		return 0;
	return 1;
}


//comandi disponibili nel gioco
void help() 
{
	printf("  Sono disponibili i seguenti comandi:\n"
		"* !help --> mostra l'elenco dei comandi disponibili\n"
		"* !who --> mostra l'elenco dei client connessi al server\n"
		"* !connect <nome_client> --> avvia una partita con l'utente nome_client\n"
		"* !disconnect --> disconnette il client dall'attuale partita intrapresa con un altro peer\n"
		"* !comb <comb> --> permette al client di fare un tentativo con la combinazione comb\n"
		"* !quit --> disconnette il client dal server\n");
}

//comandi disponibili durante una partita
void helpm() 
{
	printf("  Sono disponibili i seguenti comandi:\n"
		"* !help --> mostra l'elenco dei comandi disponibili\n"
		"* !disconnect --> disconnette il client dall'attuale partita intrapresa con un altro peer\n"
		"* !comb comb --> permette al client di fare un tentativo con la comb comb\n"
		"* !quit --> disconnette il client dal server\n");
}


//formato messaggi
//messaggio per il comando !quit
//formato e\0
void exit_mess(int mess)
{
	if(mess==1)	//essendo in partita devo avvisare l'avversario
	{
		printf("Uscita dal server: HAI PERSO!\n");
		UDP_buf_out[0]=DISC_CODE;
		UDP_buf_out[1]='\0';
		UDP_send();
	}
	close(UDP_sk);		//chiudere il socket udp
	close(server_sk);	//e chiudere quello che avevo con il server
	play=0;		//termino il ciclo while di match()
	busy=0;		//non sono più occupato con un altro giocatore
	printf("Arrivederci!\n");
	exit(0);
}

//messaggio per la richiesta di login: nome
//formato l<nome>\0
void login_mess()	
{
	TCP_buf_out[0]=LOGIN_CODE; 
	TCP_buf_out[1]='\0';
    	strcat(TCP_buf_out, kb_buf);
	TCP_send();
}

//messaggio per la richiesta di login: porta
//formato p<port>\0
void port_mess()
{
	TCP_buf_out[0]=PORT_CODE;
	TCP_buf_out[1]='\0';
	strcat(TCP_buf_out, kb_buf);
	TCP_send();
}

//messaggio per la richiesta dell'elenco degli utenti disponibili
//formato w\0
void who_mess()
{
	TCP_buf_out[0]=WHO_CODE;
	TCP_buf_out[1]='\0';
	TCP_send();
}

//messaggio per la richiesta di iniziare una partita con un avversario
//formato g<opp_name>\0
void play_mess()
{
	TCP_buf_out[0]=PLAY_CODE;
	TCP_buf_out[1]='\0';
	strcat(TCP_buf_out, opp_nickname);
	TCP_send();
}

//formato ry\0
void yes_mess()
{
	TCP_buf_out[0]=RESPP_CODE;
	TCP_buf_out[1]=OK_CODE;
	TCP_buf_out[2]='\0';
	TCP_send();
}

//formato rn\0
void no_mess()
{
	TCP_buf_out[0]=RESPP_CODE;
	TCP_buf_out[1]=NO_CODE;
	TCP_buf_out[2]='\0';
	TCP_send();
}

//messaggio in cui invio una comb
//formato a<comb>\0
void send_comb_mess()
{
	UDP_buf_out[0]= ATTACK_CODE;
	UDP_buf_out[1]='\0';
	strcat(UDP_buf_out, &kb_buf[6]);	// comando !comb comb: la comb segreta è a partire dal
						// quattordicesimo carattere
	UDP_send();
}

//messaggio in cui comunico il risultato di una comb proposta
//formato o<right><wrong>\0
void outcome_mess()
{
	UDP_buf_out[0]=OUTCOME_CODE;
	UDP_buf_out[1]='\0';
	strcat(UDP_buf_out, &right);
	strcat(UDP_buf_out, &wrong);
	UDP_send();
}

//messaggio del server al comando !connect nome di un utente sfidante
//formato g<opponent>\0
void play_req()
{	
	//TCP_buf_in[0]==PLAY_CODE
	copy(opp_nickname, &TCP_buf_in[1], '\0');
	printf("%s chiede se vuoi giocare\n", &TCP_buf_in[1]);
	printf("Digitare Y per confermare o N per rifiutare\n>");
	play_resp();
}

//risposta ad un messaggio di sfida
void play_resp()
{
	kb_read();
	if(kb_buf[0] == 'y' || kb_buf[0] == 'Y')
	{
		yes_mess();
		return;
	}	
	else
	{
		if(kb_buf[0] == 'n' || kb_buf[0] == 'N')
		{
			printf("\n> ");
			no_mess();
			return;
		}
		else 
		{
			printf("Risposta non valida\n> ");
			play_req();
		}			
	}
}

//risposta del server alla !who
//formato wn\0 oppure w<list>\0
void who_resp()
{
	//TCP_buf_in[0]==WHO_CODE
	if(TCP_buf_in[1]==NO_CODE)
	{
		printf("Non ci sono altri utenti on-line.\n\n>");
		return;
	}
	else
	{
		//TCP_buf_in[1]==OK_CODE
		printf("%s \n>", &TCP_buf_in[2]); //lista restituita dal server;
		return;
	}
}

//messaggio di disconnessione
//formato d\0
void disc_mess(int mess)
{
	if(mess==1)	//l'utente ha deciso di abbandonare la partita
	{
		printf("Hai deciso di abbandonare la partita.\nHAI PERSO!\n");
		printf("Attendi che l'avversario riceva la notifica...\n");
		UDP_buf_out[0]=DISC_CODE;
		UDP_buf_out[1]='\0';
		play=0;			//termino il ciclo while in match
		UDP_send();
		UDP_recv();
	}
	else	//mess==0 la disconnessione è avvenuta correttamente
	{	
		busy = 0;			//non sono piu' impegnato con alcun utente
		TCP_buf_out[0]=DISC_CODE;	//aggiorna la lista dei giocatori connessi e con stato libero
		TCP_buf_out[1]='\0';
		TCP_send();
	
		//predispongo il passaggio dal socket UDP al socket TCP	
		FD_ZERO(&master);	//resetto tutti i bit in master
		FD_ZERO(&read_fds);
		FD_SET(0, &master);		//setto il bit relativo alla tastiera
		FD_SET(server_sk, &master);	//setto il bit relativo al socket TCP con il server
		max_des = server_sk;
		printf("Avvenuta disconnessione dalla partita in corso.\n\n> ");
	}
	return;
}


//gestione dei messaggi in ingresso durante una partita
void UDP_sk_cmd()
{
	int g=0, s=0;
	char app[4];
	strcpy(app, sec_comb);


	if(UDP_buf_in[0]==ATTACK_CODE)	//lo sfidante ha proposto una comb che deve essere esaminata
	{
		g = find_right(&UDP_buf_in[1], app);
		s = find_wrong(&UDP_buf_in[1], app);
		if(attempt == 1)
		{
			if(g==4)
			{
				printf("\nTu e %s avete pareggiato.\n", opp_nickname);
				attempt=0;
				play=0;	//termino il ciclo while in match()
			}
			else
			{
				printf("\nHAI VINTO!\n");
				attempt=0;
				play=0;	//termino il ciclo while in match()
			}
		}
		else //attempt==0
		{
			if(g==4)
			{
				printf("\n%s ha indovinato la tua comb!\n"
					" Hai un solo tentativo da adesso\n", opp_nickname);
				attempt = 1;
			}
		}
		right = g+'0';
		wrong = s+'0';
		outcome_mess();
		turn = 'm';
	}


	if(UDP_buf_in[0]==OUTCOME_CODE)	//lo sfidante ha analizzato la comb che 
					//gli ho inviato e mi ha fornito un esito
	{
		if(attempt==0)
		{
			if(UDP_buf_in[1]=='4')	//right==4
			{
				printf("\nCOMPLIMENTI! Hai indovinato la comb di %s.\n" 
					"A %s rimane un solo tentativo\n", opp_nickname, 						opp_nickname);
				attempt=1;
				timer();
				return;
			}
			else
			{
				char ag[2], as[2];
				ag[0] = UDP_buf_in[1];
				as[0] = UDP_buf_in[2];
				ag[1] = '\0';
				as[1] = '\0';
				printf("\nCi sono:\n\n");
				printf("%s NUMERI CORRETTI IN POSIZIONE CORRETTA\n\n", ag);
				printf("%s NUMERI CORRETTI MA IN POSIZIONE SBAGLIATA\n\n", as);
				timer();
				return;
			}
		}
		else	//attempt==1
		{
			if(UDP_buf_in[1]=='4')
			{
				printf("\nTu e %s avete pareggiato.\n", nickname);
				attempt=0;
				play=0;	//termino in ciclo while in match()
				return;
			}
			else
			{
				printf("\nHAI PERSO!\n");
				attempt=0;
				play=0;	//termino in ciclo while in match()
				return;
			}
		}
	}


	if(UDP_buf_in[0]==DISC_CODE)
	{
		printf("\n%s ha abbandonato la partita.\n HAI VINTO!\n", opp_nickname);
		busy = 0;	//non più impegnato con alcun utente
		play = 0;	//termino il ciclo while in match()
		UDP_buf_out[0]= OK_CODE;
		UDP_buf_out[1]='\0';
		UDP_send();
		return;
	}
}


void kb_cmd()
{
	if(strcmp("!help", kb_buf)==0)	//richiesta per l'elenco dei comandi disponibili
	{
		if(busy==0)	//non impegnato con alcun avversario
		{
			help();
			printf("\n>");
		}
		else
		{	
			helpm();
			printf("\n#");
		}
		return;	
	}


	if(strcmp("!who", kb_buf)==0) //richiesta per la lista degli utenti connessi
	{
		if(busy==0)
			who_mess();
		else
			printf("Il comando !who non può essere usato in partita.\n");
		return;
	}


	if(strncmp("!connect", kb_buf, strlen("!connect"))==0)	//comando per la richiesta di sfidare 
															//l'utente indicato
	{
		if(busy==0)
		{
			if(kb_buf[8]==' ' && kb_buf[9]!='\0')	//!connect <nome>
			{
				memset(opp_nickname, 0, MAX_BUFFER);
				
				if (strcmp(nickname, &kb_buf[9])==0)	//autosfida
				{
					printf("Nome sbagliato: non si può sfidare se stessi.\n> ");
					return;
				}
				
				copy(opp_nickname, &kb_buf[9], '\0');
				play_mess();
				block = 1;	//impedisco che qualcun'altro faccia richiesta 
							//di giocare con me finchè non so il responso
				wait();
				return;
			}
			else	//gestione errori
			{
				if(kb_buf[8]!=' ')
				{						
					printf("Comando errato: lasciare uno spazio tra !connect"
						" e nome utente\n> ");
					return;
				}
				if(kb_buf[9]=='\0')
				{
					printf("Comando errato: specifica il nome utente dopo !connect"
						" es.: '!connect <nomeutente>'\n> ");
					return;
				}
			}
		}
		else	//busy==1
		{
			printf("Il comando !connect non puo' essere usato in partita\n ");
			return;
		}
	}


	if(strncmp("!comb", kb_buf, strlen("!comb"))==0)//comando per attaccare
																	//l'avversario (solo in partita
																	//avviata)
	{
		if(play==0)	//devo trovarmi dentro il ciclo while controllato dalla variabile play
		{
			printf("Il comando !connect non puo' essere usato fuori da una partita.\n> ");
			return;
		}
		if(turn=='m')
		{
			if(kb_buf[5]==' ' && kb_buf[6]!='\0')	//!comb <comb>
			{
				if(check_comb(&kb_buf[6])==-1)
				{
					printf("Formato della comb errato."
						" Digitare di nuovo la comb\n");
					return;
				}
				else	//il controllo ha dato esito positivo
				{
					send_comb_mess();
					turn='n';
				}
				return;
			}
			else	//gestione errori
			{
				if(kb_buf[5]!=' ')
				{						
					printf("Comando errato: lasciare uno spazio prima"
						" della comb\n ");
					return;
				}
				if(kb_buf[6]=='\0')
				{
					printf("Comando errato: specificare la comb dopo"
						" !comb es.: '!comb <comb>'\n");
					return;
				}
			}
			return;
		}
		else	//turn==n
		{
			printf("Non e' ancora il tuo turno\n");
			return;
		}
	}


	if(strcmp("!disconnect", kb_buf)==0)
	{
		if(busy==0)
		{
			printf("Il comando !disconnect non puo' essere usato fuori da una partita. \n> ");
			return;
		}
		else
		{
			disc_mess(1);	//partita terminata forzatamente, 
							//perchè un utente ha deciso di abbandonare la partita
			return;
		}
	}


	if(strcmp("!quit", kb_buf)==0)		//chiusura della connessione con il server
	{
		if(busy==0)
			exit_mess(0);
		else
			exit_mess(1);	//va avvisato l'avversario prima di uscire
		return;
	}


	//else
	printf("Comando non valido.\n");
	if(busy==0)
		printf("> ");
}


void match()
{
	int a, ret;
	printf("Digita la tua comb segreta: ");
	a=1;
	while(1)
	{
		kb_read();
		a=check_comb(kb_buf);	//verifica la correttezza della comb scelta
		if(a==-1)	//comb scorretta
		{
			printf("La comb che hai scelto ha un formato errato!\n"
				" Digita la tua comb segreta: ");
			a=1;	//consente di digitare una nuova comb
		}
		else
		{
			if(a==0)	//comb valida
			break;
		}
	}
	copy(sec_comb, kb_buf, '\0');

	printf("Partita avviata!!\n\n");

	//predispongo il socket UDP
	max_UDP_des = UDP_sk;
	fd_set fds_UDP;
	FD_ZERO(&fds_UDP);
	timer();	//imposta il timer per lo scadere del tempo nella scelta di una comb d'attacco
	play=1;

	while(play)
	{
		if(turn=='m')	//il mio turno
		{
			printf("\nE' il tuo turno\n\n# ");
		}
		else	//turno dell'avversario
		{
			if(attempt==0)
				printf("\nE' il turno di %s\n\n# ", opp_nickname);
		}

		FD_SET(0, &fds_UDP);		//controllo il descrittore relativo alla tastiera
		FD_SET(UDP_sk, &fds_UDP);	//e relativo al socket UDP
		ret=select(max_UDP_des+1, &fds_UDP, NULL, NULL, &time);	//la select rimane attiva per un minuto
		if(ret==-1)
		{
			printf("\nFallita la funzione select() in partita");
			return;
		}
		if(ret==0)	//timer scaduto
		{
			if(turn=='n')	//turno dell'avversario
			{
				printf("\nScaduto il tempo di risposta del tuo avversario: HAI VINTO!\n");
			}
			else
			{
				printf("\nTempo scaduto: HAI PERSO!\n");
			}
			play=0;	//termino il ciclo while in match()
		}

		// la select ha ritornato il descrittore pronto
		if(FD_ISSET(UDP_sk, &fds_UDP))		//dati dal socket UDP
		{
			UDP_recv();
			UDP_sk_cmd();
			timer();
		}
		if(FD_ISSET(0, &fds_UDP))	//dati da tastiera
		{
			kb_read();
			kb_cmd();
		}
	}
	printf("FINE PARTITA\n");
	disc_mess(0);	//partita terminata regolarmente
}


//il server sbambia i parametri di entrambi gli utenti, l'uno con l'altro
//formato h<turn><IP>:<port>\0
void param_resp()
{
	//TCP_buf_in[0]==PARAM_CODE in formato IP:porta
	turn = TCP_buf_in[1];
	copy(opp_ip, &TCP_buf_in[2], ':');		//copia fino a :
	copy(opp_UDP_port, &TCP_buf_in[last+3], '\0');	//last è l'indice dei :
													//leggo dal carattere successivo
	memset(&opp_par, 0, sizeof(opp_par));
	opp_par.sin_family=AF_INET;
	opp_par.sin_port=htons((short)atoi(opp_UDP_port));
	if(inet_aton(opp_ip, &opp_par.sin_addr)==-1)
	{
		printf("Fallita la funzione inet_aton() che configura i parametri dell'avversario\n");
		exit_mess(0);	//non sto ancora in partita
		return;
	}
	printf("Scambio dei parametri completato\n\n");
	busy=1;		//da ora in poi io e il mio avversario siamo occupati (scelta della comb
				//e poi la partita vera e propria)	
	match();	//scelta della comb e inizio della partita
}

//messaggio del server relativo alla risposta data dall'utente sfidato
//formato ry\0 o rn\0
void respp_resp()
{
	//buf_TCP_in[0]==RESPP_CODE
	block = 0;	//è arrivata la risposta
	if(TCP_buf_in[1]==OK_CODE)
	{
		printf("PARTITA ACCETTATA\n");
		return;
	}
	else
	{
		if(TCP_buf_in[1]==NO_CODE)
		{
			printf("Partita rifiutata\n> ");
			return;
		}
		else
		{
			printf("Utente non trovato: inesistente o occupato.\n> ");
		}
	}
}


//gestisce quali azioni effettuare sulla base dei messaggi ricevuto dal server o da altri utenti
void receive()                                                                                                            
{
	if (TCP_recv()<0)
	{
		printf("Non e' stato ricevuto tutto il messaggio\n");
		exit_mess(0);
		return;
	}
	
	if(TCP_buf_in[0]==WHO_CODE)	//risposta del server alla !who
	{
		who_resp();
		return;
	}
	if(TCP_buf_in[0]==PLAY_CODE)	//messaggio del server al comando !connect nome di un utente sfidante
	{
		play_req();
		return;
	}
	if(TCP_buf_in[0]==RESPP_CODE)	//messaggio del server relativo alla risposta data dall'utente sfidato
	{
		respp_resp();
		return;
	}
	if(TCP_buf_in[0]==PARAM_CODE)	//il server sbambia i parametri di entrambi gli utenti, l'uno con l'altro
	{
		param_resp();
		return;
	}
	if(TCP_buf_in[0]==CRASH_CODE)	//l'avversario si è disconnesso
	{
		printf("\nATTENZIONE! Nella partita precedente l'utente %s si e' disconnesso a causa di un crash.\n\n> ", 				opp_nickname);
	}
}


//funzione main
int main(int argc, char* argv[])
{ 
	setbuf(stdout, NULL);	//prova a prevenire il buffering su std output

  	if(argc!=3) //controllo che gli argomenti passati al main siano 3
  	{
		printf("ERRORE MAIN: uso ./mastermind_client <indirizzo_IP> <porta>\n");
		exit(1);
  	}

	port= atoi(argv[2]);
  	if(port>65535 || port<1024)		//controllo la validità della porta passata dall'utente
  	{
		printf("ERRORE: porta TCP non valida\n");
		exit(1);
  	}

	test = test_IP(argv[1]);		//controllo la validità dell'indirizzo IP passato dall'utente
  	if(!test)
  	{
		printf("ERRORE: indirizzo IP non valido\n");
		exit(1);
  	} 

	//creazione del socket TCP per la comunicazione con il server
	server_sk = socket(AF_INET, SOCK_STREAM, 0); 

	//inizializzazione server_par
	memset(&server_par, 0, sizeof(server_par)); 
  	server_par.sin_family=AF_INET;			
	server_par.sin_port=htons((short)atoi(argv[2]));
	ret=inet_pton(AF_INET, argv[1], &server_par.sin_addr);	//conversione IP_server al formato network
	if(ret==-1)
	{
		printf("Fallita la funzione inet_pton() \n");
		exit(1);
	}

	ret=connect(server_sk, (struct sockaddr*)&server_par, sizeof(server_par));	//connessione al server
	if(ret==-1)
	{
		printf("Fallita la funzione connect() \n");
	    	exit(1);
	}
  	else	
  	{
		printf("\n\nConnessione al server %s sulla porta %s effettuata con successo.\n\n",
			argv[1], argv[2]);
	    
		//eseguo la richiesta di login al server
		for(;;)	//con questo ciclo infinito, aspetto che il cliente digiti un nome valido 
				//per eseguire il login
		{
			printf("Inserisci il tuo nome: ");
			kb_read();	
			login_mess();		
			strcpy(nickname, kb_buf);

			//gestione della risposta dal server
			if(TCP_recv(server_sk)<=0)
      	    		{
				printf("Ricezione incompleta\n");
      	    		}
            		else
            		{
			    	if(TCP_buf_in[0]==OK_CODE)
			    	{
			    		memset((void*)kb_buf, 0, MAX_BUFFER);	//predispongo il buffer della
																//tastiera per la prossima lettura
			    		break;
			    	}
		    		else
		    		{
		    			printf("Nome gia in uso, riprovare\n");
		    			memset((void*)kb_buf, 0, MAX_BUFFER);	
		    			memset((void*)TCP_buf_out, 0, MAX_BUFFER);
		    		}	
            		}
    		}

		//inserimento nome accettato, attesa inserimento della porta
    		for(;;)	//con questo ciclo infinito, aspetto che il cliente digiti una 
					//porta valida per eseguire il login
    		{
        		printf("\nInserisci la tua porta UDP: ");
	    		kb_read();
	    		port=atoi(kb_buf);	//converto il valore della porta da carattere a intero
	    		if(port>65535 || port<1024)
	    		{
	    			printf("Porta non valida: riprovare\n");
	    		}
	    		else
	    		{
	    			memset((void*)TCP_buf_out, 0, MAX_BUFFER);
	    			port_mess();	
	    			strcat(UDP_port, kb_buf);

				//gestione risposta dal server
	    			if(TCP_recv(server_sk)<0)
	    			{
	    				printf("Ricezione incompleta\n");
	    			}
	    			else
	    				if(TCP_buf_in[0]==OK_CODE)
	    					break;	
	    		}
	}
    
	printf("\nLOGIN CONFERMATA\n\n");
   	busy = 0;	//l'utente appena connesso non è subito impegnato in una partita
	
	UDP_sk = socket(AF_INET, SOCK_DGRAM, 0); 	//creazione del socket per la comunicazione UDP
	if(UDP_sk==-1)
	{
		printf("Fallita la creazione del socket UDP\n");
		exit(1);
	}
	memset(&my_par, 0, sizeof(my_par));
    	my_par.sin_family=AF_INET;
    	my_par.sin_port=htons((short)atoi(UDP_port));
    	inet_aton(argv[1], &my_par.sin_addr);

	//comportamento da server del peer: collegamento dell'indirizzo su cui il peer accetta le richieste
	if((bind(UDP_sk, (struct sockaddr*)&my_par, sizeof(my_par)) == -1))	
	{
		printf("Fallita la funzione bind() sul socket UDP\n");
		exit(1);
    	}

	//predispongo il socket TCP per comunicare con il server e la lettura da tastiera
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(0, &master); 			//setto il bit relativo allo standard da tastiera
	FD_SET(server_sk, &master); 	//setto il bit relativo al socket del server
	max_des = server_sk;


	printf("\n\n--------------------------------------------------------------------------------\n");
	printf("                         Benvenuto a MasterMind!\n");
	printf("--------------------------------------------------------------------------------\n");
	help();
	printf("--------------------------------------------------------------------------------\n");
	printf("\n\nIn attesa di istruzioni:\n\n");
	printf(">");
	
	for(;;)
	{
		if(busy==0)	//non sono ancora impegnato con nessun altro utente
			read_fds = master;	//copio l'array di modo che la select non alteri 
								//l'array originale con i descrittori settati
		if(select(max_des+1, &read_fds, NULL, NULL, NULL)==-1)
		{
			printf("Fallita la funzione select()\n");
			exit(1);
		}
		for(i=0; i<= max_des; i++)
		{
			if(FD_ISSET(i, &read_fds))	//cerca i descrittori ancora settati dopo la select
			{
				if(i==0)	//fd della tastiera
				{
					if(block==0)
					{
						memset((void*)kb_buf, 0, MAX_BUFFER);
						kb_read();
						kb_cmd();
					}
					else	//block=1 significa che l'utente ha fatto richiesta 
							//di una partita e sta aspettando una risposta, 
							//quindi se scrive un comando nell'attesa, questo 
							//non viene eseguito	
					{
						kb_read();
						wait();
					}
				}

				//messaggi provenienti dal server o da uno sfidante/sfidato 
				//(relativi al socket TCP)
				if(busy==0)
				{
					if(i==server_sk)
						receive();
				}
			}
		}
	 }
  }
}
