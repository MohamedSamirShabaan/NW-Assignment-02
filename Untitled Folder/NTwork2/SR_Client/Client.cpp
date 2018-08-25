#include <iostream>
#include <inttypes.h>
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
#include <errno.h>
#include <signal.h>


#define ECHOMAX 512
#define TIMEOUT_SECS 2
#define PACKET_SIZE 500


using namespace std;

struct packet {
    /* Header */
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data[500]; /* Not always 500 bytes, can be less */
};

/* Ack-only packets are only 8 bytes */
struct ack_packet {
    uint16_t cksum; /* Optional bonus part */
    uint16_t len;
    uint32_t ackno;
};

/* External error handling function */
void DieWithError(string errorMessage) {
	perror (errorMessage.c_str());
	exit(1);
}

int send_udp(int socket, const void *msg, unsigned int msgLength, struct sockaddr_in & destAddr) {
	return sendto(socket, msg, msgLength, 0,(struct sockaddr *)& destAddr, sizeof(destAddr));
}



int tries=0;
void CatchAlarm(int ignored)
{
    tries += 1;
}

int receive_window=0;
struct sockaddr_in echoServAddr; /* Echo server address */
ofstream myfile;
string file_request;


uint16_t setCheckSum(void * file_pck,bool from_client)
{
        //return 0;
        int cnt;
        if(from_client){
        cnt=sizeof(ack_packet);
        }

        else{
        cnt=sizeof(packet);
        }

        uint16_t  * addr=(uint16_t *) file_pck;
        register long sum = 0;

        while( cnt > 1 )  {
           /*  This is the inner loop */
               sum += (uint16_t) *addr++;
               cnt -= 2;
       }

           /*  Add left-over byte, if any */
       if( cnt > 0 )
               sum += * (unsigned char *) addr;

           /*  Fold 32-bit sum to 16 bits */
       while (sum>>16)
           sum = (sum & 0xffff) + (sum >> 16);

       uint16_t checksum;
       checksum = ~sum;

       return checksum;
}

bool validateCheckSum(packet file_pck)
{
    packet new_pck;
    memset(&new_pck,0,sizeof(packet));
    memcpy(new_pck.data,file_pck.data,PACKET_SIZE);

    new_pck.len=file_pck.len;
    new_pck.seqno=file_pck.seqno;

    return setCheckSum(&new_pck,false)==file_pck.cksum;
}

double time_receive=0.0;

void start_rcv_file(int sock)
{
    printf("received 1st packet\n");
    int rcv_base=0;
    unsigned int fromSize=sizeof(echoServAddr);
    int respStringLen;

    vector <packet> buffer;
    vector <bool> received;
    memset(&received,0,sizeof(received));
    clock_t before = clock();
    while(true)
    {
        packet pck_file;
        if((respStringLen = recvfrom(sock, &pck_file, sizeof(pck_file), 0,
                                     (struct sockaddr *) &echoServAddr, &fromSize)) <0)
        {
            continue;
        }
        if(pck_file.len==0)
            break;

        if(!validateCheckSum(pck_file))
            continue;
        if(pck_file.seqno < rcv_base+receive_window)
        {
            ack_packet ack;
            memset(&ack,0,sizeof(ack_packet));
            ack.ackno=pck_file.seqno;
            ack.len=0;
            ack.cksum=setCheckSum(&ack,true);
            send_udp(sock,&ack,sizeof(ack),echoServAddr);
            if(pck_file.seqno>=rcv_base)
            {
                if(pck_file.seqno>=received.size())
                {
                    received.resize(pck_file.seqno+1,false);
                    buffer.resize(pck_file.seqno+1,pck_file);
                }
                received[pck_file.seqno]=true;
                buffer[pck_file.seqno]=pck_file;
            }
        }
        while(received[rcv_base] && rcv_base<received.size())
            rcv_base++;

    }

    clock_t after = clock();
    time_receive = (after- before) / (double) CLOCKS_PER_SEC * 1000;


    myfile.open(file_request.c_str());
    printf("file recived \n");
    printf("# of packets :%d",buffer.size());
    int cnt=0;
    for(int i=0; i<buffer.size(); i++)
        for(int j=0; j<buffer[i].len; j++)
        {
            myfile << buffer[i].data[j];
            cnt++;
        }
    printf("bytes read: %d\n",cnt);
    printf("file written");

    myfile.close();
}

void receive_first_packet(int sock, string requestString)
{
    //Send eqquest
    packet pck;
    memset(&pck,0,sizeof(packet));
    struct sockaddr_in fromAddr;/* Source address of echo */
    for(int i=0; i<requestString.length(); i++)
    {
        pck.data[i]=requestString[i];
    }
    pck.data[requestString.length()]='\0';
    pck.len=requestString.length()+1;
    printf("requested file: %s length: %d\n",pck.data,pck.len);
    pck.seqno=0;
    pck.cksum=setCheckSum(&pck,false);
    int seqno=0;
    int x = send_udp(sock, &pck, sizeof(pck), echoServAddr);
    /* Recv a response */
    unsigned int fromSize = sizeof(echoServAddr) ;
    int recvd_seq=!seqno;
    int respStringLen=0;
    /* Recv a response */
    packet pck_file;

   alarm(TIMEOUT_SECS);
    while((respStringLen = recvfrom(sock, &pck_file, sizeof(pck_file), 0,(struct sockaddr *) &echoServAddr, &fromSize)) <0)
    {
        if (errno==EINTR)
        {
            x = send_udp(sock, &pck, sizeof(pck), echoServAddr);
            alarm(TIMEOUT_SECS);
            printf("client timed out can't find server!\n");
        }
        else
        {
            DieWithError("recvfrom() failed");
        }
    }


    alarm(0);
    start_rcv_file(sock);
}

double start_client(int port,string server_ip, string file_name,int window)
{
    time_receive=0.0;
    file_request=file_name;
    printf("starting client of selective repeat\n");
    int sock;/* Socket descriptor */
    struct sockaddr_in fromAddr;/* Source address of echo */
    unsigned short echoServPort  = port; /*Echo server port */
    unsigned int fromSize; /*In-out of address size for recvfrom() */
    string servIP = server_ip ;	/*IP address of server */
    string requestString = file_name; /*String to send to echo server */
    char echoBuffer[ECHOMAX]; /*Buffer for receiving ack of request */
    int respStringLen; /* Length of received response */
    struct sigaction myAction;
    receive_window=window;
    /* Create a datagram/UDP socket */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        DieWithError( "socket () failed") ;
    }
    myAction.sa_handler = CatchAlarm;
    if (sigfillset(&myAction.sa_mask) < 0) /* block everything in handler */
        DieWithError( "sigfillset () failed") ;
    myAction.sa_flags = 0;
    if (sigaction(SIGALRM, &myAction, 0) < 0)
        DieWithError("sigaction() failed for SIGALP~") ;
    /* Construct the server address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr)); /* Zero out structure */
    echoServAddr.sin_family = AF_INET; /* Internet addr family */
    echoServAddr.sin_addr.s_addr = inet_addr(servIP.c_str()); /* Server IP address */
    echoServAddr.sin_port = htons(echoServPort); /* Server port */
    /* Send the request to the server */

    receive_first_packet(sock,file_name);
    close(sock);
    return time_receive;
}


int main(int argc, char *argv[]) {
	ifstream openFile;
    string server_ip;
    string server_port;
    string client_port;
    string file_name;
    string receive_window;

    openFile.open("client.in");

	getline(openFile,server_ip);
	getline(openFile,server_port);
	getline(openFile,client_port);
	getline(openFile,file_name);
	getline(openFile,receive_window);
	
    int n = 5;
    double times[n];
    double average = 0;
    for(int i = 0; i < n; i++) {
        times[i] = start_client(atoi(server_port.c_str()), server_ip, file_name, atoi(receive_window.c_str()));
        average += times[i];
        sleep(2);
    }
    average /= n;

    printf("test result for %d times is  average : %f\n",n, average);
    for(int i = 0;i < n; i++) {
        printf("%f ", times[i]);
    }
    printf("\n");
  
	return 0;
}
