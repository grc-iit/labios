# README #

##TODO
* Organize the code
    * Make a generic abstract (pure virtual) interface in 
    common/client_interface.
    * Make all external client implement the abstract class.
* Implement Porus Client Service(Future task)
    * simply route all calls through this guy
* Finish Porus lib 
    * application registration
    * test write and read to and from Porus Client (Memcached and NATS)
* Complete Task Scheduler
    * Decide on strategies for Task Scheduler
    * finalize code for how to do pick up task and put in worker queue
* Complete Worker Programs
    * Integrate worker programs into code

* Run end to end test


##Setup Porus
### Memcached
#### Client
`memcached -p 11211 -l localhost -d`
#### Server
`memcached -p 11212 -l localhost -d`
### NATS
#### Client
`./gnatsd -p 4222 -a localhost -l ~/nats_client.log &`
#### Server
`./gnatsd -p 4223 -a localhost -l ~/nats_server.log &`

