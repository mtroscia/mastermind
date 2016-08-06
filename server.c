//------------------------------------------------------------//
/*---------------------MASTERMIND SERVER---------------------*/
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


//astrazione del cliente lato server
struct client_conf
{
	struct sockaddr_in cli_config;	
  	char nickname[MAX_BUFFER];	//se è NULL, il server è in attesa della login dell'utente
  	int user_state;		   	//0=utente disconnesso, 1=utente connesso, 2=utente occupato in una partita
  	short UDP_port;		   	//porta UDP
  	int opponent;		    //indice dell'avversario in client_list
  	int my_socket;		    //socket collegato alla porta TCP dell'utente
	int logged;		      	//login completata
};


//array contenente gli utenti connessi al server
struct client_conf client_list[MAX_USERS]; 


char TCP_buf_in[MAX_BUFFER];	//buffer TCP di ingresso
char TCP_buf_out[MAX_BUFFER];	//buffer TCP di uscita

struct sockaddr_in server_par, client_par;

fd_set master;		//insieme dei descrittori di socket
fd_set read_fds;	//usato durante la select per non sporcare master
int max_des;		//numero massimo di descrittori che possono essere controllati con la select

socklen_t client_add_len;

int server_sk;	//socket del server
int conn_sk;	//socket di connessione usato dalla accept()

int ret;


//funzioni per invio/ricezione pacchetti
int TCP_send(int socket)
{
	  int howmany;
	  howmany= send(socket, (void*)TCP_buf_out, MAX_BUFFER, 0);
	  if(howmany<MAX_BUFFER) 
	  {
	  	printf("\nTRASFERIMENTO INCOMPLETO\n");
	    return -1;
	  }
	  return howmany;
}

int TCP_recv(int socket)
{					
	int in;
	if((in = recv(socket,(void*)TCP_buf_in,MAX_BUFFER,0))<MAX_BUFFER)
	{
		return -1;
	}
	return in;
}


//funzioni di utilità
//converto un intero in ascii; non uso sprintf perchè altrimenti mi si sfalsa il messaggio
void conv_int(int val, char* str) 
{
	char num[10]={'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
	char arr[11];
	int n, i, j;
	
	i=0;
	while(val!=0)
	{
		n = val%10;			//restituisce le unità
		val = val-n;		//azzero le unità
		val = val/10;		//elimino le unità
		arr[i] = num[n];	//scrivo le unità: scrivo il numero da destra a sinistra
		i = i+1;
	}
	j=0;
	
	while(i!=0) 	//ciclo per copiare il numero da arr a str correttamente
	{
		str[j]=arr[i-1];
		j=j+1;
		i=i-1;
	}
	str[j]='\0';
}

void copy(char* elem1, char* elem2, char sym)
{
	int i=0;
	while(elem2[i]!= sym)
	{
		elem1[i]=elem2[i]; 
		i++;
	}
	elem1[i]='\0';
}

//verifica la validità dell'IP del server
int test_IP(char* ip)
{
	struct sockaddr_in try;
	int res;
	res = inet_pton(AF_INET, ip, &(try.sin_addr.s_addr));
	if(res<=0)
		return 0;
	return 1;
}


//formato messaggi
//(rec indica a chi va mandato il messaggio)
//formattazione messaggio y\0
void ok_mess(int rec)	
{
	TCP_buf_out[0]=OK_CODE;
	TCP_buf_out[1]='\0';
	TCP_send(rec);
}

//formattazione messaggio n\0
void no_mess(int rec)	
{
	TCP_buf_out[0]=NO_CODE;
	TCP_buf_out[1]='\0';
	TCP_send(rec);
}

//messaggio che include la lista degli utenti attivi
//formattazione messaggio wy<nickname> --> e' in partita con un altro utente  
//oppure wy<nickname> --> disponibile
void who_ok_mess(int index)
{
	strcat(TCP_buf_out, client_list[index].nickname);
	strcat(TCP_buf_out, " --> \t");
	if(client_list[index].user_state==2)
	{
		strcat(TCP_buf_out, "in partita con un altro utente\n");
	}
	else
	{
		strcat(TCP_buf_out, "disponibile\n");
	}
}

//messaggio che comunica che non ci sono utenti disponibili al di fuori di se stesso
//formattazione messaggio wn\0
void who_no_mess()
{
	TCP_buf_out[0]=WHO_CODE;
	TCP_buf_out[1]=NO_CODE;
	TCP_buf_out[2]='\0';
}

//messaggio che dice che non si può giocare contro se stessi
//formattazione messaggio gn?\0
void who_err_mess(int rec)
{
	TCP_buf_out[0]=PLAY_CODE;
	TCP_buf_out[1]=NO_CODE;
	TCP_buf_out[2]='?';
	TCP_buf_out[3]='\0';
	TCP_send(rec);
}

//messaggio che indica che il giocatore è gia accupato in un'altra partita
//formattazione messaggio gnb\0
void who_busy_mess(int rec)
{
	TCP_buf_out[0]=PLAY_CODE;
	TCP_buf_out[1]=NO_CODE;
	TCP_buf_out[2]='b';
	TCP_buf_out[3]='\0';
	TCP_send(rec);
}

//messaggio per la !connect
//formattazione messaggio g<opponent>\0
void chall_mess(int rec, int index)	
{
	TCP_buf_out[0]=PLAY_CODE;
	TCP_buf_out[1]='\0';
	strcat(TCP_buf_out, client_list[index].nickname);	//index è lo sfidante
	printf("%s\n", TCP_buf_out);
	TCP_send(rec);
	printf("Invio inoltrato\n");
}

//messaggio di risposta alla !connect per dire che non si è trovato l'utente
//formattazione messaggio r?\0
void opp_not_found_mess(int rec)
{
	TCP_buf_out[0]=RESPP_CODE;
	TCP_buf_out[1]='?';
	TCP_buf_out[2]='\0';
	TCP_send(rec);
}

//messaggio di risposta alla sfida
//copia esatta di quello che mi è arrivato dallo sfidato
void resp_chall_mess(int rec)
{
	TCP_buf_out[0]= TCP_buf_in[0];
	TCP_buf_out[1]=	TCP_buf_in[1];
	TCP_buf_out[2]= TCP_buf_in[2];
	TCP_send(rec);
}

//messaggi per i giocatori della partita
//formattazione messaggio h<turn><ip>:<port>\0
void par_mess(int rec, int index, char turn) 
{
	char app_port[MAX_BUFFER];

	TCP_buf_out[0]=PARAM_CODE;
	TCP_buf_out[1]='\0';
	strcat(TCP_buf_out, &turn);
	strcat(TCP_buf_out, inet_ntoa(client_list[index].cli_config.sin_addr));	//ip
	strcat(TCP_buf_out,":\0");
	conv_int((int)client_list[index].UDP_port, app_port);					//porta UDP
	strcat(TCP_buf_out, app_port);
	TCP_send(rec);
}

//il server prepara i parametri da inviare ai rispettivi giocatori per la loro partita
//formattazione messaggi a<ip>:<UDP_port>\0
void exch_par_mess(int sen)	
{	
	printf("Parametri sfidato %s ", client_list[sen].nickname);
	par_mess(client_list[sen].my_socket, client_list[sen].opponent, 'm');	//inizia lo sfidato
	printf("inviati correttamente\n\n");
	printf("Parametri sfidante %s ", client_list[client_list[sen].opponent].nickname);
	par_mess(client_list[client_list[sen].opponent].my_socket, sen, 'n');
	printf("inviati correttamente\n\n");

	//i due giocatori sono occupati nella partita da ora in poi
	client_list[sen].user_state = 2;
	client_list[client_list[sen].opponent].user_state = 2;
}


//elenco dei comandi che il client può inviare al server e che questo deve gestire
void cmd(int sen_sk) 
{
	int i;			//indice all'interno di lista clienti per trovare il mittente del messaggio
	char app_buf[MAX_BUFFER]; 	//buffer d'appoggio per ricopiare una parte di buf_TCP_in
	int j, m, app;

	m=-1;

	for(i=0; i<MAX_USERS; i++) 	//cerco il mittente all'interno di lista_clienti
  	{
    		if(client_list[i].my_socket==sen_sk && client_list[i].user_state!=0)	//non c'è motivo di
																	//voler comunicare con il server se
																	//sono impegnato con un utente!
		break;
	}

	if(i==MAX_USERS)
	{
    		printf("\nUtente non trovato\n");
    		exit(1);
  	}


  	switch(TCP_buf_in[0]) //elenco dei messaggi che il server deve gestire
  	{
		//richiesta per il nome della login
		case LOGIN_CODE:
        			memset(TCP_buf_out, 0, MAX_BUFFER);
				memset(app_buf, 0, MAX_BUFFER);
				copy(app_buf, &TCP_buf_in[1], '\0');	//nome scelto
		    		
				for(j=0; j<MAX_USERS; j++)
		      		{
					if((strcmp(app_buf, client_list[j].nickname)==0) //nome in uso da
						&& client_list[j].user_state!=0)    //un utente impegnato
			  			break;
		      		}
			      	if(j==MAX_USERS) //non ci sono nomi duplicati
		      		{
    					printf("Non ci sono utenti con questo nome: NOME ACCETTATO\n");
    					strcpy(client_list[i].nickname, app_buf);
    					ok_mess(sen_sk);
    					break;
		      		}
		      		else 	//trovato il duplicato
		      		{
                     //e ho occupato la stessa posizione di un altro utente che aveva 
					//il mio stesso nome e ora è disconnesso (user_state==0)
    					if(client_list[j].my_socket==sen_sk)
       					{
    			  			printf("Stesso nome di un utente precedentemente"
							" disconnesso: NOME ACCETTATO\n");
    			  			ok_mess(sen_sk);
    						break;
    					}
    					else
    					{
    			  			printf("\nIl nome scelto gia' esistente: "
							"NOME RIFIUTATO\n");
    			  			no_mess(sen_sk);
    		      				break;
    					}
		      		}
		
		//richiesta per la porta della login
		case PORT_CODE:
				memset(TCP_buf_out, 0, MAX_BUFFER);
				memset(app_buf, 0, MAX_BUFFER);
				app = atoi(&TCP_buf_in[1]); 
				if(app>65535 || app<1024)
  				{
					no_mess(sen_sk);
					printf("La porta scelta non e' compresa tra 1024 e 65535:"
						" PORTA RIFIUTATA\n");
					break;
  				}
				
				printf("PORTA ACCETTATA\n");
				client_list[i].UDP_port = (short)atoi(&TCP_buf_in[1]);
	        		inet_ntop(AF_INET, (void*)&client_list[i].cli_config.sin_addr.s_addr,
					app_buf, INET_ADDRSTRLEN);
	    			ok_mess(sen_sk);
				client_list[i].logged=1;

	    			printf("\nLogin completata: nome %s porta %d\n\n", 
					client_list[i].nickname, client_list[i].UDP_port); 
	    			break;
		      		
		//il client ha effettuato il comando !who
		case WHO_CODE:
				printf("\n%s richiede comando !who\n", client_list[i].nickname);
				memset(TCP_buf_out, 0, MAX_BUFFER);
				TCP_buf_out[0]=WHO_CODE;
				TCP_buf_out[1]=OK_CODE;
				TCP_buf_out[2]='\0';

				for(j=0; j<MAX_USERS; j++)
				{
					if(client_list[j].logged==1)	//login completa
					{
						if(client_list[j].user_state!=0 
							&& (strcmp(client_list[j].nickname, client_list[i].nickname))!=0)
						{
							m=0;	//indica che c'è almeno un utente che non è il 
									//mittente del codice who_code
							who_ok_mess(j);	//messaggio che include la lista degli utenti attivi
						}
					}
				}
				if(m==-1)	//non ci sono altri utenti al di fuori del mittente. 
							//formattazione messaggio un\0
					who_no_mess();
				TCP_send(sen_sk);
				break;
	
		//il cliente ha effettuato il comando !connect
		case PLAY_CODE:
				memset(TCP_buf_out, 0, MAX_BUFFER);
				memset(app_buf, 0, MAX_BUFFER);
				copy(app_buf, &TCP_buf_in[1], '\0');

				for(j=0; j<MAX_USERS; j++)
				{
					if(client_list[j].logged==1)	//login completa
						if(strcmp(client_list[j].nickname, app_buf)==0 
							&& client_list[j].user_state==1)
							break;
				}
				if(j==MAX_USERS)
				{
					opp_not_found_mess(sen_sk);
					break;
				}
				
				//setto preventivamente lo stato di entrambi i giocatori "in partita" per
				//evitare che altri utenti facciano richiesta di sfida, 
				//mentre lo sfidante è in attesa di risposta
				client_list[i].user_state=2;
				client_list[j].user_state=2;
				client_list[i].opponent=j;
				client_list[j].opponent=i;
				printf("\n%s lancia una sfida a %s\n", 
					client_list[i].nickname, client_list[j].nickname);
				
				TCP_buf_out[0]=PLAY_CODE;
				TCP_buf_out[1]='\0';
				strcat(TCP_buf_out, client_list[i].nickname);

				//invio della richiesta allo sfidato
				TCP_send(client_list[j].my_socket);	
				break;

		// risposta dello sfidato alla !connect			
		case RESPP_CODE:
				memset(TCP_buf_out, 0, MAX_BUFFER);

				//invio della risposta allo sfidante
				resp_chall_mess(client_list[client_list[i].opponent].my_socket);  
				if(TCP_buf_in[1]==NO_CODE)
				{
					printf("\nPartita rifiutata\n\n");

					//rendo di nuovo liberi gli utenti
					client_list[i].user_state=1;
					client_list[client_list[i].opponent].user_state=1;
					client_list[i].opponent=-1;
					client_list[client_list[i].opponent].opponent=-1;
				}
				else
				{
					printf("\nPartita accettata\n\n");
					exch_par_mess(i);
				}
				break;
	
		//il client ha effettuato il comando !disconnect			
		case DISC_CODE:
				if(client_list[i].user_state==2)	//occupato con un altro utente
				{
					printf("\n%s ha lasciato la partita contro di %s\n", 
						client_list[client_list[i].opponent].nickname,
							client_list[i].nickname);
					client_list[i].user_state=1;
					client_list[i].opponent=-1;
				}
				break;
  	}
}


//funzione main
int main(int argc, char* argv[])
{ 
	setbuf(stdout, NULL);	//prova a prevenire il buffering su std output	

	int port, test;
	int i, j;
	int opt, rec_bytes;
	
	opt=1;	//usato nella setsockopt

	if(argc!=3)
  	{
		printf("ERRORE MAIN: uso ./mastermind_client <indirizzo_IP> <porta>\n");
		exit(1);
  	}

  	port = atoi(argv[2]);
  	if(port > 65535 || port < 1024)
  	{
		printf("ERRORE: porta TCP non valida\n");
		exit(1);
  	}

  	test = test_IP(argv[1]);
  	if(!test)
  	{
		printf("ERRORE: indirizzo IP non valido\n");
  	}

	FD_ZERO(&master);	
  	FD_ZERO(&read_fds);

	//configurazione e inizializzazione del socket del server
	printf("\nSERVER AVVIATO\n\n\n");
	memset(&server_par, 0, sizeof(server_par));
  	server_sk= socket(AF_INET, SOCK_STREAM, 0);
	if(server_sk==-1)
	{
		printf("Fallita socket()\n");
		exit(-1);
	}
	
	//consento di riutilizzare un indirizzo precedentemente assegnato (fase di esecuzione)
  	if(setsockopt(server_sk, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int))==-1)	
  	{
    		printf("Fallita setsockopt()\n");
    		exit(1);
  	}
  	server_par.sin_family = AF_INET;
	server_par.sin_port = htons((short)atoi(argv[2]));
  	inet_pton(AF_INET, argv[1], (void*)&server_par.sin_addr.s_addr);

	
	//collegamento dell'indirizzo IP al socket: specifica l'indirizzo su cui il server 
	//accetta le richieste	
	ret=bind(server_sk, (struct sockaddr*)&server_par, sizeof(server_par));
  	if(ret == -1)	//fallisce se la porta è gia occupata
  	{
    		printf("Fallita bind() su server_sk\n");
    		exit(1);
  	}
	printf("Funzione bind() eseguita correttamente\n\n");

	
	//il socket si pone in attesa di connessioni
	ret=listen(server_sk, 100);
  	if(ret == -1)
  	{
    		printf("Ci sono piu' richieste di quante la sk_server possa gestire: chiusura forzata.\n");
    		exit(1);
  	}
	FD_SET(server_sk, &master);	//metto in ascolto il socket del server per accettare nuove 
								//richieste di connessione
  	max_des = server_sk;	//all'accenzione del server avrò solo sk_server come fds
  	printf("Server in ascolto...\n");


    for(;;)
    {
        read_fds = master;	//read_fds serve come copia di master

		//la select si blocca finchè non arriva almeno una richiesta
        if((select(max_des+1, &read_fds, NULL, NULL, NULL))==-1)
        {
        		printf("Fallita select()\n");
        		exit(1);
        }

		//si controlla in quale socket è stato scritto qualcosa; 
		//i è l'indice dell socket nell'array master
		for(i=0; i<= max_des; i++)		
		{
          	if(FD_ISSET(i, &read_fds))
          	{
   	      		if(i==server_sk)//il listening socket ha una richiesta pendente
				{
					printf("Richiesta di una nuova connessione!\n");
            		for(j=0; j<MAX_USERS;j++)
            		{
						//configurazione dello stato iniziale dell'utente 
						//appena connesso
						if(client_list[j].user_state==0)  //occupo il primo posto libero nell'array
						{
            		      	client_list[j].user_state=1;	
            		      	client_list[j].opponent = -1;	
            			  	client_list[j].logged=0;	

            		      	client_add_len = sizeof(client_list[j].cli_config);
	            		    memset(&client_list[j].cli_config, 0, client_add_len);
            		      				
							//creazione di un nuovo socket per l'utente appena connesso
							if((conn_sk=accept(server_sk, (struct sockaddr*)&client_list[j].cli_config,
								&client_add_len))== -1)
            		      		{
                             		printf("Connessione fallita\n");
            			     		exit(1);
            		      		}
					    	client_list[j].my_socket=conn_sk;

					    	printf("NUOVA CONNESSIONE ACCETTATA\n\n");
					    	FD_SET(conn_sk, &master);
					    	if(conn_sk > max_des)
					    	{
            			    	max_des = conn_sk;
            		      	}
            		      	break;
            		    }
            	    }

                	if(j==MAX_USERS)
                	{
						printf("Capacita' esaurita\n");
                	    exit(1);
                	}
            	}

		        else //richiesta su fd di una connessione già esistente
		        {
                	rec_bytes=TCP_recv(i);
                	if(rec_bytes > 0)
                	{
                		cmd(i); //i corrisponde a 'my_socket'
                	}

					//il cliente si è disconnesso oppure non sono arrivati tutti i bytes
					else 
					{
                	   	for(j=0; j<MAX_USERS; j++)	//ricerca dell'utente
						{
							if(client_list[j].my_socket==i)
							//non ho ricevuto dati per un minuto dal socket:
							//utente disconnesso
                	    	{
                	    		printf("L'utente %s si e' disconnesso.\n\n",									 client_list[j].nickname);
								
								//eliminazione del suo descrittore e del socket
                	   			FD_CLR(i, &master); 
                	   			close(i);
									
								//segnalazione all'avversario della disconnessione
                			    if(client_list[j].user_state==2)	
								{
									TCP_buf_out[0]=CRASH_CODE;
									TCP_buf_out[1]='\0';
									TCP_send(client_list[client_list[j].opponent].my_socket);
								}
								
								//libera il posto nell'array client_list
								client_list[j].logged=0;
               			      	client_list[j].user_state=0;
								client_list[j].opponent=-1;
							}
						}
            	    }
                }
         	}
		}
	}
}

