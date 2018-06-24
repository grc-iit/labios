# README #

##TODO LIST
* Default solver
* Small I/O cache
* Wake up and suspend workers

##Notes
* Timeout task scheduling. line#66
* check the usleep in task scheduler-> infinite looping
* listener_thread-> scheduler_thread->1solver:2solver:3solver->1sender

##TODO CODE
* IDs everywhere (done ID= timestamp)
* Debugged with many cases for logical bugs (done)
* Metadata updates and creation (done)
* build configuration manager to change ip and ports on command line. (done)
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
* Task scheduling 
    * fixed scheduling with read data (done)
    * with greedy
        * approach 1: make current dp greedy
        * approach 2: make customized greedy
* Worker Manager Service
    * Perform automated task balancing of worker.
    * Build a sorted list of worker scores.
    * Wake and sleep workers (based on events using nats(WORKER_MANAGER tasks))
* Worker Service
    * Update worker scores (done)
* System Manager Service
    * Setup Application registrations
* code cleanup
##TODO INVESTIGATION
* Investigate worker suspension
* Investigate read simulation
* Automated server bootstrapping
* Cluster environment: 1 Lib, 1 TaskSched, 2 worker

#VCS management
##Make sure we work on local versions for constants.h
`cd aetrio/`

`git update-index --assume-unchanged src/common/constants.h 
src/common/configuration_manager.h src/common/configuration_manager.cpp CMakeLists.txt`

##Undo commands:
`cd aetrio/`

`git update-index --no-assume-unchanged src/common/constants.h 
 src/common/configuration_manager.h src/common/configuration_manager.cpp CMakeLists.txt`


#Setup Aetrio

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
