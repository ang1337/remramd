# remramd
REMote RAM Disk - remotely accessible sandboxed virtual storage device.

# Project structure

```
.
├── build_client.sh
├── build_server.sh
├── client
│   ├── CMakeLists.txt
│   ├── include
│   │   └── client_core.hpp
│   └── src
│       ├── client_core.cpp
│       └── remramd_client.cpp
├── driver
│   ├── Makefile
│   └── ramd.c
├── LICENSE
├── README.md
├── scripts
│   └── chroot_jail_builder.py
└── server
    ├── CMakeLists.txt
    ├── include
    │   └── remramd_server.hpp
    └── src
        ├── remramd_server.cpp
        └── remramd_server_main.cpp
```

```./driver``` - contains block device driver that represents fully functional RAM disk that uses I/O multiqueue.

```./client``` - remramd client that gets a remote shell from the server via providing server's IP address and TCP port.

```./server``` - remramd fork server that manages a minimal CLI and client processes

```./build_client.sh``` & ```./build_server.sh``` - client/server compilation scripts, uses CMake behind the scenes. Server compilation script additionally compiles the driver.

```./scripts``` - initially contains chroot jail builder script that will build an individual sandboxed environment for each client.

# How it works?

- Driver
RAM disk implementation via modern block subsystem API using I/O multiqueue.

- Client
    1) Randomizes valid TCP port
    2) Sends this port to the server
    3) Waits for server's permission
    4) If declined - exits, else - gets a remote shell via ```netcat``` inside of the chroot jail. Each client uses a RAM disk as a block device for storing it's data in chroot jail.

    Print ```exit``` to disconnect from the server.

- Server (high level overview)
    1) Creates a thread that constantly accepting new connection attempts.
    2) If new client wants to connect, the notifiation is popped on the server showing IP address and port for remote shell for this client.
    3) There is an option to drop the pending connection request via one of the menu options.
    4) If server side wants this client to connect, the server forks a new process, creates a chroot jail for it, cd and chroots into the prepared jail, drops privileges from root to server side defined and opens a reverse shell for the client inside of the isolated environment with server side defined amount of exposed binaries for each client individually.

    At any point of time, the server can print out all current clients, drop a specific client, check for pending connections etc. 
    
# Dependencies
- Python 3 
- Linux kernel version >= 5.0
- gcc
- cmake
- netcat

# Demo

Client ```127.0.0.1:33889``` - local client process on another terminal tab

Client ```10.0.0.4:6640``` - my smartphone (Android 10) that runs ```remramd_client```

```~/testjail``` is RAM disk mount point

```~/testjail/127.0.0.1:33889``` - local client process jail fake root

```~/testjail/10.0.0.4:6640``` - my smartphone's client jail

![2020-10-14-01-04-46](https://user-images.githubusercontent.com/45107680/95921820-aa77fb00-0dba-11eb-867a-988eab772ddf.gif)

# How to use it?

- Server side

Compile the server and the driver by running ```chmod +x build_client.sh && ./build_client.sh```.

First of all, go to ```driver``` directory and enter ```sudo insmod ramd.ko``` in order to load the driver into the kernel. ```lsblk``` command will show a new device ```/dev/ramd``` which is a given RAM disk. You can provide a size for the disk via setting ```ramd_size``` argument, e.g. ```sudo insmod ramd.ko ramd_size=500``` will create 512 MB RAM disk (disk size is aligned to 2^x bytes). The default capacity is 1 GB.

Then you need to create a partition that will be formatted further. ```sudo fdisk /dev/ramd``` to partition the device (go for a default suggestions). ```lsblk``` will show ```/dev/ramd1``` partition. Now the partition needs to be formatted to one of the filesystem formats, let's pick a well-known ```ext4```. Enter ```sudo mkfs -t ext4 /dev/ramd1```. Now, the partition is ready to be mounted anywhere in your filesystem. Create an arbitrary directory and mount the formatted partition on it, e.g. ```mkdir -p ~/jail && sudo mount /dev/ramd1 ~/jail```. Now RAM disk is ready to be used as a separate virtual block device.

In order to run the server, enter ```sudo ./server/build/remramd_server <TCP port to listen for clients> <path to current Python 3 interpreter> <path to chroot jail builder script>```.

```sudo ./server/build/remramd_server 1337 `which python` ./scripts/chroot_jail_builder.py``` - the server will listen on port 1337.

- Client side

Compile the client by running ```chmod +x build_client.sh && ./build_client.sh```.

You need to know server's IP address and TCP listening port. Usage: ```./client/build/remramd_client <server IP> <port>```.
