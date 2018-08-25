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
#include <errno.h>
#include <signal.h>
#include <ctime>


#define ECHOMAX 512 /* Longest string to echo */ 

#define TIMEOUT_SECS 2


using namespace std;



ifstream openFile;
double time_taken=0.0;
struct sockaddr_in echoServAddr; /* Echo server address */
string requestt;


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


int send_udp(int socket, const void *msg, unsigned int msgLength, struct sockaddr_in & destAddr) {
	return sendto(socket, msg, msgLength, 0,(struct sockaddr *)& destAddr, sizeof(destAddr));
}


/* External error handling function */
void DieWithError(string errorMessage) {
	perror (errorMessage.c_str());
	exit(1);
}

uint16_t setCheckSum(void * file_pck,bool flag)
{
    int counter;
    if(flag)
    {
		/*checksum for ack = 8*/
        counter=sizeof(ack_packet);
    }

    else
    {
		/* checksum for file packet = 508 */
        counter=sizeof(packet);
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

    
    while (sum>>16){
		// for sign and fit in 16 bit for large data
        sum = (sum & 0xffff) + (sum >> 16);
		//cout << "here = " << sum << endl;
	}

    uint16_t checksum;
    checksum = ~sum; // get two's complment
    
    //cout << "checksum = " << checksum << endl;

    return checksum;
}

bool validateCheckSum(packet file_pck)
{
    packet new_pck;

    memset(&new_pck,0,sizeof(packet));
    memcpy(new_pck.data,file_pck.data,500);

    new_pck.len=file_pck.len;
    new_pck.seqno=file_pck.seqno;

	// compare checksum of packet with the compute checksum function
    return setCheckSum(&new_pck,false)==file_pck.cksum;
}


void recv_file (int sock)
{
    int seqno=0;
    unsigned int fromSize=sizeof(echoServAddr);
    int respStringLen;
    vector <packet> file;
    clock_t b = clock();
    
    while(true)
    {	
        //clock_t here1 = clock();
        packet pkt;
        ack_packet ack;
        ack.len=0;
        
        memset(&ack,0,sizeof(ack_packet));
        memset(&pkt,0,sizeof(packet));
        
        if((respStringLen = recvfrom(sock, &pkt, sizeof(pkt), 0,
                                     (struct sockaddr *) &echoServAddr, &fromSize)) <0){continue;}
        if(pkt.len==0)
        {
			ack.cksum = setCheckSum(&ack,true);
            send_udp(sock,&ack,sizeof(ack),echoServAddr); // send ack we received current packet and no file to rcv more
            break;
        }
        else if(pkt.seqno != seqno || !validateCheckSum(pkt)) // assum no corruption happen , received non expected packet seqno
        {
            ack.ackno=!seqno; 
        }
        else // assum no corruption happen , received expected packet seqno
        {
            ack.ackno = seqno;
            seqno = !seqno;
            file.push_back(pkt);
        }
        ack.cksum = setCheckSum(&ack,true);
        send_udp(sock,&ack,sizeof(ack),echoServAddr);
         printf("Ack sent");
         cout << ack.ackno << "\n";
        //clock_t here2 = clock();
        //double time_taken33 = (here2 - here1) / (double) CLOCKS_PER_SEC * 1000;
        //cout << "time = " << time_taken33 << "\n";
    }
    
    clock_t a = clock();
    time_taken = (a- b) / (double) CLOCKS_PER_SEC;
    
    ofstream myfile;
    myfile.open(requestt.c_str());
   
    printf("Rcev file as number of packets = :%d\n", file.size());
   
    for(int i=0; i < file.size(); i++){
		for(int j=0; j < file[i].len; j++)
		{
            myfile << file[i].data[j];
		}
	  }
    printf("finish recv file\n");
    myfile.close();
}

void connect_alarm(int ig){}


void send_file_name(int sock, string request)
{
    packet pck;
    
    
    struct sockaddr_in fromAddr;/* Source address of echo */
    
    for(int i=0; i < request.length(); i++)
    {
        pck.data[i]=request[i];
    }
    
    pck.data[request.length()]='\0';
    pck.len=request.length()+1;
    
    printf("Send File Name as Request : %s\n",pck.data);
    
    pck.seqno=0;
    
    pck.cksum = setCheckSum(&pck,false);

    int seqno=0;
    
    
     
    int x = send_udp(sock, &pck, sizeof(pck), echoServAddr);
    
    unsigned int fromSize = sizeof(echoServAddr) ;
    
    int respStringLen=0;
    
    /* Recv a response */
    packet pkt;

        alarm(TIMEOUT_SECS);
        while((respStringLen = recvfrom(sock, &pkt, sizeof(pkt), 0,(struct sockaddr *) &echoServAddr, &fromSize)) < 0)
        {
            if (errno==EINTR)
            {
                x = send_udp(sock, &pck, sizeof(pck), echoServAddr);
                alarm(TIMEOUT_SECS);
                printf("Error in find Server\n");
            }
            else
            {
                DieWithError("recvfrom() failed");
            }
        }

    alarm(0);
    recv_file(sock);
}

double open_connection(int ServerPort , string ServerIP , string File_Name , int I_WS)
{    
    printf("******************************* Start stop and wait model *************************** \n");
    
    // TCP/IP Book 
    int sock;/* Socket descriptor */
    struct sockaddr_in fromAddr;/* Source address of echo */
    unsigned short echoServPort  = ServerPort; /*Echo server port */
    unsigned int fromSize; /*In-out of address size for recvfrom() */
    string servIP = ServerIP ;	/*IP address of server */
    char echoBuffer[ECHOMAX]; /*Buffer for receiving ack of request */
    int respStringLen; /* Length of received response */
    struct sigaction myAction;
    
    /* Create a datagram/UDP socket */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        DieWithError( "socket () failed") ;
    }
    
    myAction.sa_handler = connect_alarm;
    if (sigfillset(&myAction.sa_mask) < 0) /* block everything in handler */
		{DieWithError( "sigfillset () failed") ;}
    myAction.sa_flags = 0;
    if (sigaction(SIGALRM, &myAction, 0) < 0)
        {DieWithError("sigaction() failed for SIGALP~") ;}
    
    /* Construct the server address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr)); /* Zero out structure */
    echoServAddr.sin_family = AF_INET; /* Internet addr family */
    echoServAddr.sin_addr.s_addr = inet_addr(servIP.c_str()); /* Server IP address */
    echoServAddr.sin_port = htons(echoServPort); /* Server port */
    /* Send the request to the server */
	
    send_file_name(sock,File_Name);
    close(sock);
    
    return time_taken;
}

int main(int argc, char *argv[]) {
    string ServerIP;
    string ServerPort;
    string ClientPort;
    string File_Name;
    string I_WS;

    openFile.open("client.in");

	getline(openFile,ServerIP);
	getline(openFile,ServerPort);
	getline(openFile,ClientPort); // After dicussion no problem in ubuntu so i will not use it
	getline(openFile,File_Name);
	getline(openFile,I_WS);

    int consecutiveRun = 5; 
    double times[consecutiveRun];
    double average = 0;

    for(int i = 0; i < consecutiveRun; i++) {
        printf("************************ Run number  = %d ******************** \n", i);
        time_taken = 0.0;
        requestt = File_Name;
        times[i] = open_connection(atoi(ServerPort.c_str()), ServerIP, File_Name, atoi(I_WS.c_str()));
        average += times[i];
        sleep(10);
    }
    average /= consecutiveRun;
	
	cout << "************** Time Taken for each consecutive run = *********** \n";
  
    for(int i = 0; i < consecutiveRun ; i++) {
        printf("%f ", times[i]);
    }
    
    printf("\nAverage Consecutive Run : %f\n", consecutiveRun , average);
	return 0;
}
