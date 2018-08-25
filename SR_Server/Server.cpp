#include <iostream>
#include <fstream>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <errno.h>
#include <map>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <bitset>        
#include <math.h>
#include <ctime>
#include <queue>



#define ECHOMAX 512 /* Longest string to echo */
#define TIMEOUT_USECS 300
#define PACKET_SIZE 500



using namespace std;

double global_window_size=0;
double global_ssthresh=0;
clock_t timeout = clock_t(TIMEOUT_USECS * (pow(10,-6))*CLOCKS_PER_SEC);
int tries=0;
vector<sockaddr_in> serving;
ofstream result("congestion.txt");
ofstream result2("ss.txt");

char tmp[100];

/* Ack-only packets are only 8 bytes */
struct ack_packet {
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t ackno;
};


struct packet {
    /* Header */
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data[PACKET_SIZE]; /* Not always 500 bytes, can be less */
};

/*to store each packed with related time */
class Pair
{
public:
    clock_t time;
    packet pck;
    Pair(clock_t time, packet pck)
        : time(time), pck(pck) {}
};

/*to sort each packet depending on it's time*/
class PairCompare
{
public:
    bool operator()(const Pair &t1, const Pair &t2) const
    {
        return t1.time > t2.time;
    }
};

struct send_data_thread_args
{
    sockaddr_in client; /*client socked details*/
    packet pck_file; /*data packed*/
    int loss_probability; /*loss probability used in PLP*/
    int seed; 
};

struct send_pck_thread_args
{
    pthread_mutex_t * timer_mutex; /*mutex change packed time*/
    pthread_mutex_t * state_mutex; /*mutex change window and state*/
    priority_queue<Pair, vector<Pair>, PairCompare>* my_queue;
    /*to contain packet and time related with this packed*/
    int packets;
    bool * acknowledged;
    bool * finished; /*finished from send all file or not*/
    sockaddr_in client;
    int socket; /*communication socked*/
    double * window_size; /*window size*/
    int * current_state; /*current state  0,1 */
    double * ssthresh; /*threshould*/
    int * send_base; /*current send base*/
    int loss_probability; 
    int seed;
};


/* External error handling function */
void DieWithError(string errorMessage) {
	perror (errorMessage.c_str());
	exit(1);
}


int send_udp(int socket, const void *msg, unsigned int msgLength, struct sockaddr_in & destAddr , int loss_probability) {
	int prob=(rand()%(100)+1);
    packet* pck=(packet *)msg;
	if(prob > 100-loss_probability) {return -10;}
	return sendto(socket, msg, msgLength, 0,(struct sockaddr *)& destAddr, sizeof(destAddr));
}

uint16_t setCheckSum(void * file_pck,bool flag)
{
    int counter;
    if(flag)
    {
		/*checksum for ack = 508*/
        counter=sizeof(packet);
    }

    else
    {
		/* checksum for file packet = 8 */
        counter=sizeof(ack_packet);
    }

    uint16_t  * addr=(uint16_t *) file_pck;
    //cout << "addr" << * addr << endl;
    
    register long sum = 0;

    while( counter > 1 )
    {
		//cout << "sum =" << sum << "counter = " << counter <<  "address = " << *addr << endl;
        sum += (uint16_t) *addr++; // to get each address byte
       
        counter -= 2; // move by two
    }

    if( counter > 0 )
        sum += * (unsigned char *) addr; // for last byte

    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16){
		// for sign and fit in 16 bit for large data
        sum = (sum & 0xffff) + (sum >> 16);
		//cout << "here = " << sum << endl;
	}

    uint16_t checksum;
    checksum = ~sum; // get two's complment
    
    //cout << "checksum = " << checksum << endl;

    packet *pck=(packet *)file_pck;
    
    return checksum;
}

bool validateCheckSum(ack_packet file_pck)
{
    ack_packet new_pck;

    memset(&new_pck,0,sizeof(ack_packet));

    new_pck.len=file_pck.len;
    new_pck.ackno=file_pck.ackno;

	// compare checksum of packet with the compute checksum function
    return setCheckSum(&new_pck,false)==file_pck.cksum;
}


void * receive_ack(void * args)
{
    send_pck_thread_args * arg =(send_pck_thread_args *) args;
    sockaddr_in client = arg->client;
    int sock = arg->socket;
    bool * acknowleged = arg->acknowledged;
    bool * finished = arg->finished;
    int loss_probability = arg->loss_probability;
    int seed = arg->seed;
    pthread_mutex_t * state_mutex = (arg->state_mutex);
    double * window_size = arg->window_size;
    int * state = arg->current_state;
    double * ssthresh = arg->ssthresh;
    int * send_base = arg->send_base;
    int count = 0;
    int prev = 0;
    
    while (! *(finished)){
        ack_packet ack;
        int recvMsgSize;
        unsigned int cliAddrLen = sizeof(client);
        int flag = 0;
        if ((recvMsgSize = recvfrom(sock, &ack, sizeof(ack), 0,(struct sockaddr *) &client, &cliAddrLen)) < 0){DieWithError("recvfrom() failed");}
        else{ // reveived ACK
			 printf("ack received");
		     cout << ack.ackno << "\n";
			if(validateCheckSum(ack)){
				//cout<<"Ahmed___________base: "<< *(send_base)<<" ______ACK_no: "<<ack.ackno <<" _____________________________________\n";
				if(*(send_base) < (int)ack.ackno){
					count++;
					if(count >= 3){
						pthread_mutex_lock (state_mutex);
						*(window_size) = (*(window_size)) / 2;
						if(*(window_size) <= 1.0){*(window_size) = 1;}
						if (*(window_size) != prev){
								result<<*(window_size)<<endl;
								result2<<*(ssthresh)<<endl;
							}
						 prev = *(window_size);
						if(*(window_size) > *(ssthresh)){ *(state) = 1;}
						else {*(state) = 0;}
						//printf("Ahmed______________________________________________________\n");
						//resend pck
						//result<<*(window_size)<<endl;
						pthread_mutex_unlock (state_mutex);					
						count = 0;
						flag = 1;
					}
					if(!acknowleged[ack.ackno]){
							acknowleged[ack.ackno] = true;
							if (flag)continue;
							pthread_mutex_lock (state_mutex);
					/* update  window size and state */
						//printf("Mohamed______________________________________________________\n");
						if(*(state) == 0){
							*(window_size) =  (*(window_size)) + 1.0 / (*(window_size));
							//cout << "here33333333333333333****\n";
							if(*(window_size) > *(ssthresh)){ *(state) = 1;}
						}else{
							if(*(window_size) < global_window_size){*(window_size) = *(window_size) + 1.0;}
						}
						//result<<*(state)<<","<< *(window_size)<<"," <<*(ssthresh)<<endl;
						if (*(window_size) != prev){
								result<<*(window_size)<<endl;
								result2<<*(ssthresh)<<endl;
							}
						 prev = *(window_size);
						pthread_mutex_unlock (state_mutex);
						
						}
				}else if(!acknowleged[ack.ackno]){
					acknowleged[ack.ackno] = true;
					pthread_mutex_lock (state_mutex);
					/* update  window size and state */
					//printf("Mohamed______________________________________________________\n");
					if(*(state) == 0){
						*(window_size) =  (*(window_size)) + 1.0 / (*(window_size));
						//cout << "here22222222222222222 ****\n";
						if(*(window_size) > *(ssthresh)){ *(state) = 1;}
					}else{
						if(*(window_size) < global_window_size){*(window_size) = *(window_size) + 1.0;}
					}
					if (*(window_size) != prev){
								result<<*(window_size)<<endl;
								result2<<*(ssthresh)<<endl;
							}
						 prev = *(window_size);
					pthread_mutex_unlock (state_mutex);
				}
			}
            
        }
    }
}

void send_packet(void * args, int *lost, clock_t previous)
{
	send_pck_thread_args * arg = (send_pck_thread_args *) args;
    int sock = (arg->socket);
    sockaddr_in client = arg->client;
    double * window_size = arg->window_size;
    double * ssthresh = arg->ssthresh;
    pthread_mutex_t * state_mutex = (arg->state_mutex);
    int * state = arg->current_state;
    pthread_mutex_t * timer_mutex=  (arg->timer_mutex);
    priority_queue<Pair, vector<Pair>, PairCompare>* my_queue=arg->my_queue;
    bool * acknowledged= (arg->acknowledged);
    bool * finished= (arg->finished);
    int loss_probability = arg->loss_probability;
    int * send_base = arg->send_base;
	
	clock_t current = clock();
	if( (current - previous) > (timeout/10) && (*my_queue).size() > 0){
		previous = current;
		bool has_loss = false;
		pthread_mutex_lock (timer_mutex);   
		Pair p = (*my_queue).top();
		int max_seq = *(send_base) + (int) *(window_size) + 1;
		while( p.time < current ){
			(*my_queue).pop();
			if(!acknowledged[p.pck.seqno]){
				if(p.pck.seqno <= max_seq) {
					printf("timeout send again = ");
				   cout << p.pck.seqno << "\n";
					send_udp(sock ,&p.pck,sizeof(p.pck) ,client ,loss_probability);
					*lost = *lost + 1;
				}
				p.time = current + timeout;
				(*my_queue).push(p);
				has_loss = true;
			}
			if( (*my_queue).size() > 0 ){
				p = (*my_queue).top();
			}else{
				break;
			}
		}
		pthread_mutex_unlock (timer_mutex);
		if(has_loss){
			pthread_mutex_lock (state_mutex);
			if ((*(window_size)) > 1.0){
				*(ssthresh) = (*(window_size))/2;
				*(window_size) = 1.0;
				if( *(state) != 0){*(state) =! (*(state));}
				result<<*(window_size)<<endl;
				result2<<*(ssthresh)<<endl;
			}
			pthread_mutex_unlock (state_mutex);
		}
	}
}

void *check_timer(void * args)
{
    printf("Start check timer thread\n");    
    send_pck_thread_args * arg = (send_pck_thread_args *) args;
    bool * finished = (arg->finished);
    clock_t previous = clock();
    int lost = 0;
    while( !( *finished ) ){
        send_packet((void*)args, &lost, previous);
    }
    printf("sent packets = %d lost = %d\n",arg->packets,lost);
    printf("loss rate + %f %\n",( (double)lost/(double)(arg->packets + lost))*100.0);
}


void *send_data(void * arg)
{
	/* get all args from send_data thread args struct */
    send_data_thread_args * args = (send_data_thread_args* ) arg;
    sockaddr_in client = args->client;
    packet pck = args->pck_file;
    int loss_probability = args->loss_probability;
    int seed = args->seed;
    
    int sock;	/* socket */
    int seq_num = 0; /* sequence number, start by zero*/ 
    int losses = 0; /* number of losses */
    int file_size; /* file size should be send to the client */
    
    struct sockaddr_in echoServAddr; /* local address */
    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){DieWithError("socket() failed");}
    
    printf("new Thread to serve the client request data = %s\n",pck.data);
    
    /* open the file and get file size */
    string file_name(pck.data);
    std::ifstream file;
    file.open(file_name.c_str());
    file.seekg(0, ios::end);
    file_size = file.tellg();
    file.seekg(0, ios::beg);
    
    if(file_size > 0)
    {
        char data[PACKET_SIZE];
        priority_queue<Pair, vector<Pair>, PairCompare>* my_queue = new priority_queue<Pair, vector<Pair>, PairCompare>();
        bool finished = false;
        int current_state = 0;
        double window_size_thread = 1;
        double ssthresh_thread = global_ssthresh;
        int local_window_size = (int)window_size_thread;
        int packets = (int)ceil(file_size / (double) PACKET_SIZE);
        bool acknowledged[packets];
        memset(acknowledged,0,sizeof(acknowledged));
        int send_base=0;
        int seq_num=0;
        int current_byte=0;
        int prev_percent = 0;
        pthread_mutex_t timer_mutex;
        pthread_mutex_init(&timer_mutex, NULL);
        pthread_mutex_t state_mutex;
        pthread_mutex_init(&state_mutex, NULL);
        pthread_t receive_ack_thread;
		pthread_t check_timer_thread;
        
        printf("file size = %d  ,pakcets number = %d\n",file_size,packets);
        
		/* fill args to pass them to check_timer & receive_ack */
        send_pck_thread_args * spt_args = new send_pck_thread_args();        
        spt_args->client = client;
        spt_args->acknowledged = acknowledged;
        spt_args->finished = &finished;
        spt_args->my_queue = my_queue;
        spt_args->socket = sock;
        spt_args->timer_mutex = &timer_mutex;
        spt_args->packets = packets;
        spt_args->current_state=&current_state;
        spt_args->state_mutex = &state_mutex;
        spt_args->ssthresh = &ssthresh_thread;
        spt_args->window_size = &window_size_thread;
        spt_args->send_base = &send_base;
		spt_args->loss_probability = loss_probability;
		spt_args->seed = seed;
		
		/* start new two threads to handle send packets and recved Acks */
        int ct = pthread_create(&check_timer_thread, NULL, check_timer , (void *) spt_args);
        int ra = pthread_create(&receive_ack_thread, NULL,  receive_ack, (void *) spt_args);

        while(send_base < packets){
            while(acknowledged[send_base] == true && send_base < packets){send_base++;}
            vector <packet> to_send;
            pthread_mutex_lock (&state_mutex);
            local_window_size = (int)ceil(window_size_thread);
            pthread_mutex_unlock (&state_mutex);

            while(seq_num < packets && seq_num < send_base + local_window_size){
                if(acknowledged[seq_num]){
					seq_num++;
					continue;
                }
                file.read(data, min(PACKET_SIZE,file_size-current_byte));
                packet pck_file;
                memset(&pck_file,0,sizeof(packet));
                int i;
                for(i=0; i < min(PACKET_SIZE,file_size-current_byte); i++){pck_file.data[i]=data[i];}
                current_byte += i;
                pck_file.seqno = seq_num++;
                pck_file.len = i;
                pck_file.cksum = setCheckSum(&pck_file,true);
                send_udp(sock,&pck_file,sizeof(pck_file),client,loss_probability);
                to_send.push_back(pck_file);
            }
            if(to_send.size() != 0){
                pthread_mutex_lock (&timer_mutex);
                clock_t current = clock() + timeout;
                for(int i = 0; i < to_send.size(); i++){
                    Pair p(current,to_send[i]);
                    (*my_queue).push(p);
                }
                pthread_mutex_unlock (&timer_mutex);
            }
        }
        printf("finished :D \n");
        finished = true;
        result.close();
    }/* end if(file_size > 0) */
	
    int te = -10;
    while (te == -10){
		packet t;
		memset(&t,0,sizeof(t));
		t.len = 0;
		t.seqno = 0;
		te = send_udp(sock , &t , sizeof(t),client , loss_probability);
	} 

    close(sock);
    file.close();
    printf("file closed\n");
    pthread_exit(NULL);
}

void start_connection(int port,int window,int seed,int loss_probability)
{
	
    global_window_size = (double)window;
    global_ssthresh = global_window_size/2.0;
    printf("starting server of selective repeat protocol\n");

    int sock; /* Socket descriptor */
    struct sockaddr_in echoServAddr; /* local address */
    struct sockaddr_in echoClntAddr; /* client address */
    unsigned int cliAddrLen; /* Length of incoming message */
    char echoBuffer[ECHOMAX]; /* Buffer for echo string */
    unsigned short echoServPort; /* Server port */
    int recvMsgSize; /* Size of received message */

    echoServPort = port; /* First arg' local port */

    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        DieWithError("socket() failed");
    }
    /* Construct local address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr)); /* Zero out structure */
    echoServAddr.sin_family = AF_INET; /* Internet address family */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    echoServAddr.sin_port = htons(echoServPort); /* Local port */

    /* Bind to the local address */
    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        DieWithError("bind() failed");

    printf("***************** Server Waiting for communication with Clients ************ \n");
    while(true)   /* Run forever */
    {
        /* Set the size of the in-out parameter */
        cliAddrLen = sizeof(echoClntAddr);
        /* Block until receive message from a client */
        packet recvd;
        if ((recvMsgSize = recvfrom(sock, &recvd, sizeof(recvd), 0,(struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0)
        {
            DieWithError("recvfrom() failed") ;
        }
        printf("Handling client %s\n and port is %d", inet_ntoa(echoClntAddr.sin_addr),echoClntAddr.sin_port) ;

		/*** START CODE HERE **/
		printf("Server recved File name = %s\n",recvd.data);
		
		/* CREATE NEW THREAD TO SERVE THE CLIENT (send file date) */
        pthread_t server_thread;
        send_data_thread_args *args = new send_data_thread_args();
        args->client = echoClntAddr;
        args->pck_file = recvd;
        args->loss_probability = loss_probability;
        args->seed = seed;
        int rc = pthread_create(&server_thread, NULL, send_data, (void *) args);
    }
}

int main(int argc, char *argv[]) {
	
	ifstream openFile;

    string server_port;
    string send_window;
    string random_seed;
    string loss_probability; 

    openFile.open("server.in");

	getline(openFile,server_port);
	getline(openFile,send_window);
	getline(openFile,random_seed);
	getline(openFile,loss_probability);

    start_connection(atoi(server_port.c_str()),atoi(send_window.c_str()),atoi(random_seed.c_str()),atoi(loss_probability.c_str()));

	return 0;
}


