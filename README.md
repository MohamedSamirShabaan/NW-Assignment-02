# NW-Assignment-02
- In this assignment , We are required to implement a reliable transfer
service on top of the UDP/IP protocol. In other words, we need to
implement a service that guarantees the arrival of datagrams in the
correct order on top of the UDP/IP protocol, along with congestion
control.

- we implement Stop and Wait and Selective repeat.

### How to run code using commands :
###### in client directory should contains file client.in contains :
* Server IP “127.0.0.1”
* Server Port use same Server port will define in server.in
* client port not need it in Linux kernel
* File Name we need it from server
* initialize received cwnd

###### run this command:
g++ client.cpp -o c.out
./c.out
- in server directory should contains file server.in contains :
* Server Port same port mentioned in client.in
* max received cwnd
* seed
* PLP for simulation.
###### Go to server path and type :
g++ -pthread server.cpp -o s.out
sudo ./s.out
###### Go to client path and type :
g++ client.cpp -o c.out
./c.out localhost 80

###### you can see requests and responses in terminal or can see in
each directory
