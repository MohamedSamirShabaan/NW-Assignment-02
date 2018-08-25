#include <iostream>
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
#include <fstream>
#include <vector>
#include <sstream>
#include <ctime>

#define ECHOMAX 512 /* Longest string to echo */
#define TIMEOUT_SECS 2

using namespace std;

double time_taken33 = 0;
int nums = 0;
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
    char data[500]; /* Not always 500 bytes, can be less */
};

struct thread_arg
{
    sockaddr_in client;
    packet pck_file;
    int loss_probability;
};


/* External error handling function */
void DieWithError(string errorMessage) {
	perror (errorMessage.c_str());
	exit(1);
}


int send_udp(int socket, const void *msg, unsigned int msgLength, struct sockaddr_in & destAddr , int loss_probability) {
	int prob=(rand()%(100)+1);
	if(prob > 100-loss_probability) {
		return -10;
	}
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

int send_data_to_client(sockaddr_in client , int sock , packet pck , int seqno , int number , int loss_probability)
{

    pck.seqno = seqno;
    pck.cksum = setCheckSum(&pck , true);
	
	clock_t here1;
	
	here1 = clock();
    send_udp(sock , &pck , sizeof(pck), client , loss_probability);

    int respStringLen; 

    unsigned int fromSize=sizeof(client);
    int recvd_seq = !seqno;
    int data_loss_re_send = 0;
    /* Recv a response */
    
    while(recvd_seq != seqno)
    {
        ack_packet ack;
        memset(&ack,0,sizeof(ack_packet));
        

        if((respStringLen = recvfrom(sock, &ack, sizeof(ack), 0,(struct sockaddr *) &client, &fromSize)) <0)
        {
            if (errno==EWOULDBLOCK)
            {
				here1 = clock();	
                int se = send_udp(sock, &pck, sizeof(pck), client , loss_probability);
                if (number != 0){
						printf("timeout send again = ");
						cout << pck.seqno << "\n";
						data_loss_re_send++;
				}else{
						number++;
				}
                continue;
            }
            else
            {
                DieWithError("recvfrom() failed");
            }
        }
        else
        {
            recvd_seq = ack.ackno;
            if(!validateCheckSum(ack))
            {
                recvd_seq = !seqno;
            }
            else {
				 printf("ack received");
				 cout << ack.ackno << "\n";
				 clock_t here2 = clock();
				 time_taken33 += (here2 - here1) / (double) CLOCKS_PER_SEC * 1000;
				 nums++;
            }
        }

    }
    return data_loss_re_send;
}

void *sendall(void * args)
{

    thread_arg* arg=(thread_arg* )args;
    sockaddr_in client = arg->client;
    packet pck = arg->pck_file;
	int loss_probability = arg->loss_probability;

    printf("Created Thread to serve Client request = %s\n", pck.data);
 
    int sock;
    int seqno=0;
    
    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        DieWithError("socket() failed");
    }
    
    
    // set time out
    struct timeval tv;
    tv.tv_usec = 200;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0)
    {
        perror("Error");
    }
    
    
    // open file
    string file_name(pck.data);
    ifstream file;
    file.open(file_name.c_str());
    
    // get file size
    int fileSize;
    file.seekg(0, ios::end);
    fileSize = file.tellg();
    file.seekg(0, ios::beg);
    
    
    cout << "fileSize = %d\n" << fileSize;
    int data_loss = 0;
    
    if(fileSize > 0)
    {

        char chunk[500];
        packet pkt;
        memset(&pkt,0,sizeof(packet));
        int data_readed = 0;
        
        for(int i=0; i < fileSize; i = i + data_readed)
        {
			// get min between data readed 
            data_readed = min(500 , fileSize-i);
            file.read(chunk, data_readed);
            
            for(int j=0; j < data_readed; j++)
            {
                pkt.data[j] = chunk[j];
            }
            
            pkt.len = data_readed;

            data_loss += send_data_to_client(client , sock , pkt , seqno , i , loss_probability);
            
            seqno = !seqno;
        }
    }
    
    int te = -10;
    
    while (te == -10){
		packet t;
		memset(&t,0,sizeof(t));
		t.len=0;
		t.seqno=!seqno;
		te = send_udp(sock , &t , sizeof(t),client , loss_probability);
   } 
 
	 printf("loss detected for all send = %d\n",data_loss);
	 
    double data_lossed = (double)data_loss/((double)fileSize/500);
    
    data_lossed*=100;
    printf("loss detected per packet send = %f\n",data_lossed);

    close(sock);
    file.close();
    printf("Finish from serve this client\n");
    //cout << "nums = " << nums << endl;
	cout << "time = " << time_taken33 / nums << "\n";
	nums = 0;
	time_taken33 = 0.0;
    pthread_exit(NULL);
}

void Connection(int port,int window,int loss_probability)
{   
    cout << "************** Server Stop and wait ******************\n";
    
    int sock; /* socket */
    struct sockaddr_in echoServAddr; /* local address */
    struct sockaddr_in echoClntAddr; /* clientent address */
    unsigned int clientAddrLen; /* Length of incoming message */
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
    {
		DieWithError("bind() failed");
	}
       

    printf("*********************** Server Waiting for connection ****************** \n");
    
    for(;;)
    {
        /* Set the size of the in-out parameter */
        clientAddrLen = sizeof(echoClntAddr);
        /* Block until receive message from a clientent */
        packet recv;
        
        if ((recvMsgSize = recvfrom(sock, &recv, sizeof(recv), 0,(struct sockaddr *) &echoClntAddr, &clientAddrLen)) < 0)
        {
            DieWithError("recvfrom() failed") ;
        }
       
        printf("Handling clientent %s\n", inet_ntoa(echoClntAddr.sin_addr)) ;

        printf("Receive data in Parent = %s", recv.data);

        pthread_t temp;
        thread_arg *args=new thread_arg();
        args->client = echoClntAddr;
        args->pck_file = recv;
        args->loss_probability = loss_probability;
        int rc = pthread_create(&temp, NULL, sendall, (void *) args);
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

    Connection(atoi(server_port.c_str()),atoi(send_window.c_str()),atoi(loss_probability.c_str()));
	
	return 0;
}

