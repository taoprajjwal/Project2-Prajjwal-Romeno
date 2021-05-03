# Project 2 - TCP

In this project we implemented TCP (reliable data transfer and congestion control) from the ground up.

First run the command 'make' to compile the code. The compiled code is in the "obj" folder so you would have to cd into it after you run make. In order to run the sender:

```
./rdt_sender IP-Address PortNo FileName
```

Where IP-Address, PortNo and FileName are replaced with the relevant information.

In order to run the receiver:

```
./rdt_receiver PortNo RecieverFileName
```

Where PortNo is the same as the PortNo in the sender and RecieverFileName is the file name you want to save the file as.

## Implementation Details

We read packets index-wise from the file in the sender in-place using the fseek function.

The receiver receives packets from the sender and if an out of order packet is recieved, the packet is buffered. Once the in order packet is recieved, if any packet with a sequence number consecutively higher is available in the buffer, they are added into the receiver file and then an ACK for the packet last put into the reciever file instead of the ACK for the packet just recieved from the sender is sent.
