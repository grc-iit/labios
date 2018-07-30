# README #
#TODO LIST
##cluster
* finish timers(done)
* scripting for deployment
* investigate timings first locally. Make TABIOS faster!

##Immediate
* Revisit task_builder (done)
* Fix DP solver code (done) 
* Multithreaded task scheduler (done)
* Random solver (done)
* Round robin (done)
* Default solver (pending)
* Small I/O cache (done)
* Wake up and suspend workers (manual testing)
* Delete and flush tasks (don't care)
* Test tasks from PFS
* Memory leak

##Future
* Task dependecies (don't care)
* Metadata persistent store (flush at the end)
* Automated server bootstrapping
* Investigate read simulation

##Notes
* Timeout task scheduling. line#66
* check the usleep in task scheduler-> infinite looping
* listener_thread-> scheduler_thread->1solver:2solver:3solver->1sender

##Logic
* IDs everywhere (done ID= timestamp) How do we use the IDs? Why timestamps?
* task_builder alogorithm for building read and write task
    * case 1: new write:
        * if write > 64KB && <2MB then build 1 task (done)
        * if write > 2MB then chunk into 2 MB tasks (done)
        * if write < 64KB put into small I/O
    * case 2: update written data: (done)
        * if prev write is still in client queue/written into disk
            * create a update tasks (based on existing chunks)
    * case 3: read_task: (done)
        * if data in data pool just read and return
        * if data in disk and make read request specifically from that worker.
        * if data in PFS, make read task and schedule it to any worker
* Task scheduling 
    * fixed scheduling with read data (done)
    * with greedy
        * approach 1: make current dp greedy
        * approach 2: make customized greedy
* Worker Manager 
    * Perform automated task balancing of worker.
    * Build a sorted list of worker scores.
    * Wake and sleep workers (based on events using nats(WORKER_MANAGER tasks))
* Worker Service
    * Update worker scores (done)
        * Worker interval or task count is used to trigger worker score updates
* System Manager Service
    * Setup Application registrations

#VCS management
##Make sure we work on local versions for constants.h
`cd aetrio/`

`git update-index --assume-unchanged src/common/constants.h 
src/common/config_manager.h src/common/config_manager.cpp CMakeLists.txt`

##Undo commands:
`cd aetrio/`

`git update-index --no-assume-unchanged src/common/constants.h 
 src/common/config_manager.h src/common/config_manager.cpp CMakeLists.txt`


#Setup TABIOS
#Memcached config

##libevent is a dependency : apt-get install libevent-dev
##Change in src code(forcing our own data distribution to specific server):
##copy into libmemcached/get.cc:236
    "master_server_key=atoi(group_key);"
##copy into libmemcached/storage.cc:375
    "uint32_t server_key=atoi(group_key);"
##install from source
    ./configure && make && sudo make install

##How to use:
###Specify group_key to be the actual number of the server.
###Example: If you want to use server #0, you set the key to "0" and so on options to start servers:
    -m: how much RAM to use for item storage (in megabytes) (min is 64MB)
    -d: run it as daemon
    -p: port that listens
## Memcached
###Install memached and libmemcached from normal sources
###Run memcached
#### Client
`memcached -p 11211 -l localhost -d  -I 4M`
#### Server
`memcached -p 11212 -l localhost -d  -I 4M`

##NATS
###Install
Download from https://nats.io/download/

#####Install first the server by simply copying gnatsd to /usr/local/bin

#####For C client:
`mkdir build && cd build`

`cmake ../`

`make && sudo make install`
###Run NATS with logging
#### Client 
`gnatsd -p 4222 -a localhost -DV -l ~/nats_client.log &`
#### Server
`gnatsd -p 4223 -a localhost -DV -l ~/nats_server.log &`

###Run NATS without logging
#### Client 
`gnatsd -p 4222 -a localhost -DV &`
#### Server
`gnatsd -p 4223 -a localhost -DV &`

## Other Dependencies
###zlib
`sudo apt-get install zlib1g-dev`

