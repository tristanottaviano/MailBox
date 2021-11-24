
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include<pthread.h>
#include <ctype.h> 

#define MESSAGE_MAXLEN 1024

//VARIABLES GLOBALES
//Structure message
typedef struct s_msg {
char sender[9];
char* text;
struct s_msg * next;
} msg;
//Structure mailbox
typedef struct s_mbox{
msg *first, *last;
} mbox;
//Paramètre client
typedef struct client_data_s {
int sock;
char nick[9];
mbox* box;
pthread_t thread;
struct client_data_s* prev;
struct client_data_s* next;
}client_data;
//Nombre de clients
int nb_clients;
//Premier et derrnier clients
client_data *first,*last;
//Lock MUTEX pour protéger le variables locales
pthread_mutex_t v = PTHREAD_MUTEX_INITIALIZER;


//Initialise le nombre de client à 0
void init_client(){

    nb_clients = 0;
    first = NULL;
    last = NULL;

}

//Initialise une nouvelle mail box
mbox* init_mbox(){

    mbox* box = (mbox*)malloc(sizeof(mbox));
    box->first = NULL;
    box->last = NULL;
    return box;
 
}

//CREATE MESSAGE
msg* create_msg(char* author, char* contents){

    //Alloc
    msg* message = (msg*)malloc(sizeof(msg));
    
    //Author
    strncpy(message->sender, author, 9);
    message->sender[8]='\0';
    
    //Text
    message->text = (char*)malloc((strnlen(contents,256)+1)*sizeof(char));
    strncpy(message->text, contents, (strnlen(contents,256)+1));

    //Next
    message->next = NULL;

    return message;

}

//DESTROY MESSAGE
void destroy_msg(msg* mess){

    free(mess->text);
    free(mess);

}

//Alloue le mémoire pour un client et renvois son adresse
client_data* alloc_client(int socket){


    client_data* client = (client_data*) malloc(sizeof(client_data));
    if(client == NULL) return NULL;
    client->sock = socket;
    client->next = NULL;
    client->prev = NULL;
    strcpy(client->nick, "");
    //sprintf(client->nick, "");
    client->box = init_mbox();

    //On lock pour s'assurer que nb_client ne soit pas sujet au data race
    pthread_mutex_lock(&v);

    //On "place" le client dans la liste
    if (nb_clients==0){

        first = client;
        last = client;

    }
    
    else
    {
        
        last->next=client;
        client->prev=last;
        client->next = NULL;
        last = client;

    }
    
    //On ajoute 1 au nombre de client
    nb_clients++;

    //On unlock
    pthread_mutex_unlock(&v);

    return client;

}

//Libère la mémoire allouée à un client
void free_client(client_data* client){

    //On lock pour s'assurer que nb_client ne soit pas sujet au data race
    pthread_mutex_lock(&v);

    if(client != NULL){

        //Si il y a un seul client, on le sort
        if (nb_clients==1){

            first=NULL;
            last=NULL;

        }

        //Si c'est le premeir élément
        else if (client==first){

            first = first->next;
            first->prev = NULL;

        }

        //Si c'est le dernier élément
        else if (client==last){

            last = last->prev;
            last->next = NULL;

        }

        //Sinon
        else{

            client->prev->next = client->next;
            client->next->prev = client->prev;

        }
        
    }

    //On libère les messages contenus dans la box
    msg* tmp = client->box->first;
    while (tmp != NULL){ 
        client->box->first = tmp->next;
        destroy_msg(tmp);
        tmp = client->box->first;
    }
    //On libère la box    
    free(client->box);
    //On libère le client
    free(client);
    nb_clients--;

    //On unlock
    pthread_mutex_unlock(&v);

}

//SEARCH CLIENT: Renvois le cleitn avec pour pseudo nick si il existe, NULL sinon
client_data* search_client(char* nick){

    //Variables
    client_data* tmp=first;

    for (int i=0; i<nb_clients;i++){

        if(strcmp(nick, tmp->nick)==0) return tmp;
        tmp = tmp->next;

    }

    return NULL;

}

//VALID NICK: Renvois 1 si le nickanme n'est pas déjà pris et si il est != de "".
int valid_nick(char* nick){

    //On test si c'est Alphanum
    for( int i=0; i<strlen(nick); i++ ) {

        if (isalnum(nick[i]) == 0 ) {

            return 0;

        }

    }

    //Si le nom est déjà pris
    if(search_client(nick)!=NULL) return 0;

    //Si le pseudo est vide
    if (strcmp(nick, "")==0) return 0;

    //Si il n'est pas de la bonne taille
    if (strlen(nick)>=9 || strlen(nick)<=0) return 0;

    //Sinon
    return 1;

}


//RECOIT UN MESSAGE
int receive_message(int socket, char question[MESSAGE_MAXLEN], int n){
        
    //Variables
    int ind=0;

    //On remplit question avec le message du client
    while(1){

        //On lit octet par octet
        int rc = read(socket , &question[ind], 1);

        //Si rc<0, il y a un problème ou si les message ne fini pas (>1 000 000)
        if (rc<=0 || ind>= 10e6) return -1;

        //Quand on rencontre un \n on arrête de lire
        if (question[ind]=='\n') break;

        //On ajoute 1 à l'indice
        ind++;
        
    }

    //Si le message est trop long on renvois 1
    if (ind>=n-1){

        memset(question, 0, ind);
        return 1;

    } 

    //On écrase le \n
    //On check avant si ind == 0 car on ne veut pas que le programme check pour question[-1]
    if(ind == 0) question[ind] = '\0';
    //Si ind != 0 , alors on chack si il y a \r
    else if(question[ind-1]=='\r') question[ind-1] = '\0';
    //Sinon on écrase tout simplement
    else question[ind] = '\0';

    return 0;

}

//ECHO: RENVOIS LE MESSAGE AU CLIENT
int do_echo(char* args, char* reponse, int n){

    if (args == NULL) args = "";
    snprintf(reponse, n, "Ok: %s\n", args);
    return 0;

}

//RAND: RENVOIS UN NOMBRE ALLEATOIR 
int do_rand(char* args, char* reponse, int n){

    //Variables
    char* args1 = strsep(&args, " ");
    char* args2 = args;

    //printf("[ARGS_1]: %s\t [ARGS_2]: %s\n", args1, args2);

    //Si on a "rand" on renvois un nombre random
    if (args1 == NULL && args2 == NULL) {

        snprintf(reponse, n, "Ok: %d\n", rand());

    }

    //Si on a "rand [int]" on renvois un nombre rand entre 0 et [int]
    else if(args1!=NULL && args2 == NULL){
        
        int max;

        //Si l'utilisateur rentre 0, alors on renvois "Impossible" (car %0 -> impossible)
        if (strcmp(args1, "0") == 0){

            snprintf(reponse, n, "Fail: Impossible de tirer un nombre entre 0 et 0 (exclu)\n");
            return 0;

        }

        else if ((max = atoi(args1))<=0) {

            snprintf(reponse, n, "Fail: unknown command rand %s\n",args1);
            return 0;

        }

        else snprintf(reponse, n, "Ok: %d\n", rand()%max);

    }

    //Sinon il y a des arguments en trop
    else {

        snprintf(reponse, n, "Fail: unknown command rand %s %s\n",args1, args2);

    }
        
    return 0;

}

//QUIT: DECONNECTE LE CLIENT
int do_quit(char* args, char* reponse, int n){

    return 1;

}

//LIST: DONNE LA LISTE DES CLIENTS
int do_list(char* args, char* reponse, int n){

    //Varibales
    client_data* tmp = first;

    snprintf(reponse, n, "OK:");

    for (int i=0; i<nb_clients; i++){

        snprintf(&reponse[strlen(reponse)], n-strlen(reponse), " %s |", tmp->nick);
        tmp = tmp -> next;

    }

    snprintf(&reponse[strlen(reponse)], n-strlen(reponse), "\n");

    return 0;

}

//NICK: PERMET A L'UTILISATEUR DE CHOISIR SON PSEUDO
int do_nick(char* args, client_data* client, char* reponse, int n){

    if(args == NULL){

        snprintf(reponse, n, "Fail: Vous devez rentrer un pseudo après nick!\n");        

    }

    else if (valid_nick(args)==1 ) {

        snprintf(client->nick, 9, "%s", args);        

        snprintf(reponse, n, "Ok: Votre pseudo a été modifié en %s\n", args);
        printf("L'utilisateur %s a rentré son pseudo\n", client->nick);      

    }

    else {

        snprintf(reponse, n, "Fail: Le pseudo %s n'est pas un pseudo correcte!\n", args);        

    }

    return 0;

}

//SEND: Envois un message à un utilisateur
int do_send(char* args, client_data* client, char* reponse, int n){

    //Variables
    char* args1 = strsep(&args, " ");
    char* args2 = args;
    client_data* dest = search_client(args1);

    //Si il y a les bon nombre d'argument
    if (args1 != NULL && args2 != NULL){

        //Si le destinataire existe
        if(dest != NULL){

            pthread_mutex_lock(&v);

            msg* mess = create_msg(client->nick, args2);
            mbox* box = dest->box;
            
            //CAS BOITE VIDE
            if (box->first == NULL && box->last == NULL){

                box->first=mess;
                box->last = mess;

            }

            else{

                box->last->next=mess;   
                box->last = mess;

            }

            snprintf(reponse, n, "Ok: Votre message a bien été envoyé à %s\n", dest->nick);

            pthread_mutex_unlock(&v);

        }

        else{

            snprintf(reponse, n, "Fail: Le destinataire %s est introuvable\n", args1);

        }
        

    }

    else{

        snprintf(reponse, n, "Fail: unknown command send %s\n", args);

    }

    return 0;

}

//GET: RENVOIS MESSAGE (LE FIRST)
msg* get(mbox* box){
    
    //BOITE VIDE
    if (box->first == NULL && box->first == NULL){
    
        return NULL;

    }

    //UN SEUL MESSAGE
    else if (box->first == box->last){

        msg* aux = box->first;
        box->first = NULL;
        box->last = NULL;
        return aux;

    }

    //PLUSIEURS ESSAGES
    else {  
       
        msg* aux = box->first;
        box->first = aux->next;
        return aux; 

    }

}


//RECEIVE: Lit le premier message de la box
int do_receive(char* args, client_data* client, char* reponse, int n){

    //Variables
    
    mbox* box = client->box;
    msg* message = get(box);

    //SI LA BOITE EST VIDE
    if(message == NULL){

        snprintf(reponse, n, "Ok: La messagerie est vide\n");

    }

    //SINON
    else
    {
        
        //On lit le message
        snprintf(reponse, n, "Ok: %s vous a envoyé un message: %s\n",message->sender, message->text);

        //On détruit le message après lecture
        destroy_msg(message);

    }

    return 0;

}
//TRAITE LE MESSAGE
int eval_quest(char* question, char* reponse, client_data* client, int n){ 

    //printf("[CMD]: %s\t [ARGS]: %s\n", cmd, args);

    //On sépare la question
    char* cmd = strsep(&question, " "); 
    char* args = question;


    //On analyse la requête
    if(strcmp(cmd, "echo") == 0)return do_echo(args, reponse, n);
    
    else if(strcmp(cmd, "rand") == 0) return do_rand(args, reponse, n);
    
    else if(strcmp(cmd, "quit") == 0) return do_quit(args, reponse, n);

    else if(strcmp(cmd, "nick") == 0) return do_nick(args, client, reponse, n);
    
    else if(strcmp(cmd, "list") == 0) {
        
        if (strcmp(client->nick, "")!=0) return do_list(args, reponse, n);
        else snprintf(reponse, n, "Fail: Vous devez définir un pseudo avant d'avoir accès à cette commande %s!\n", cmd);
        
    }

    else if(strcmp(cmd, "send") == 0){
     
        if (strcmp(client->nick, "") != 0) return do_send(args, client, reponse, n);
        else snprintf(reponse, n, "Fail: Vous devez définir un pseudo avant d'avoir accès à cette commande %s!\n", cmd);
    
    }

    else if(strcmp(cmd, "rcv") == 0 ) {
     
        if (strcmp(client->nick, "") !=0 ) return do_receive(args, client, reponse, n);
        else snprintf(reponse, n, "Fail: Vous devez définir un pseudo avant d'avoir accès à cette commande %s!\n", cmd);
    
    }
    
    else {

        snprintf(reponse, n, "Fail: unknown command %s.\n", cmd);

    }

    return 0;

}

//WORKER (Pour chaque thread, il va s'éxecuter)
void* worker(void* client){

    //Variables
    char question[MESSAGE_MAXLEN];
    char reponse[MESSAGE_MAXLEN];

    //On récupère les données du client
    client_data* data = (client_data*) client;

    //On recoit/traite/répond à la demande
    while(1){
        
        int rcv = receive_message(data->sock, question, MESSAGE_MAXLEN);

        //Erreur de lecture
        if( rcv < 0){
        
            perror("Could not recieve request"); 
            break;
        
        }

        //Si question trop longue
        else if (rcv == 1){

            //On rempli le string réponse
            snprintf(reponse, MESSAGE_MAXLEN, "Fail: Votre commande est trop longue (>%d)\n", MESSAGE_MAXLEN);
            //On l'envois dans le socket client (si < 0 il y a une erreur)
            if (write(data->sock, reponse, strlen(reponse))<0) {
            perror("Could not send message");
            break; 
            }
            //On continue
            continue;

        }

        int eval = eval_quest(question, reponse, data, MESSAGE_MAXLEN);

        //Erreur d'éval
        if(eval < 0) {
            
            perror("Could not understand request");
            break;
            
        }

        //Si l'utilisateur "quit"
        else if(eval==1){

            break;

        }

        //On répond
        if(write(data->sock, reponse, strlen(reponse)) < 0) {

            perror("Could not send message");
            break; 

        }
    
        //On remet question à 0 à chaque fois.
        memset(question, 0, sizeof(question));

    }

    printf("Le client %s s'est déconnecté!\n", data->nick);
    close (data->sock);
    free_client(client);

    return NULL;

}

//CLIENT ARRIVED
int client_arrived(int socket){
    
    //On créé une struct_client puis on lance un thread.
    client_data* client = alloc_client(socket);
    if(pthread_create(&client->thread,NULL,worker, (void*)client)!=0) {

        perror("Impossible de créer le thread");
        return -1;
 
    }

    //On affiche le nombre de clients connectés & la liste.
    printf("Il y a une nouvelle connexion! (%d clients connectés)\n", nb_clients);
    
    client_data* tmp = first;

    for (int i=0; i<nb_clients; i++){

        printf(">Client %d: %s\n", i+1, tmp->nick);
        tmp = tmp -> next;

    }

    return 0;

}


//LISTEN PORT
int listen_port(int portNum){

    //Init. le socket
    int server_socket = socket(PF_INET6,SOCK_STREAM, 0);
    if (server_socket == -1) {

        perror("Il y a eu une error lors de la création du socket");
        return -1;

    }

    //On gère veut éviter une erreur si l'adresse est déjà prise
    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    //Déclare et remplit l'address du socket
    struct sockaddr_in6 adress;
    if (memset(&adress,0,sizeof(struct sockaddr_in6))==NULL) return -1;
    adress.sin6_family=PF_INET6;
    adress.sin6_port=htons(portNum);

    //Lie le socket à l'IP/Port
    if (bind(server_socket,(struct sockaddr *) &adress, sizeof(adress)) ==-1){

        perror("Binding impoosible");
        return -1;

    }

    //A l'écoute d'une connexion
    if (listen(server_socket, 1024)==-1){

        perror("Ouverture impossible");
        return -1;

    }

    //Accepte la connexion des clients
    int client_socket;
    while(1){

        if ((client_socket = accept(server_socket,NULL,NULL))==-1){

            perror("Impossible d'accepter");
            return -1;

        }

        //Si un client se connecte
        if(client_arrived(client_socket)==-1) return -1;

    }

}

//MAIN
int main (int argc, char* argv[]){

    //On s'assure qu'il y a le bon nombre d'arguments
    if (argc !=2){

        printf("Erreur de syntaxe: './serveur.out [PORT]'\n");
        exit(1);

    }

    //On met le nombre de client à 0
    init_client();

    int portNum = atoi(argv[1]);
    if (portNum < 0){

        printf("Attention: [PORT] doit être un entier positif \n");
        exit(1);

    }

    //On veut s'assurer que le serveur ne se fasse pas tuer si un client se déconnecte sans prévenir!
    signal(SIGPIPE, SIG_IGN);  

    if (listen_port(portNum)==-1){

        perror("Impossible de créer le socket correctement");
        exit(1);

    }

}