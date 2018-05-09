# README #

##TODO
* Organize the code
    * Make consider_after_a generic abstract (pure virtual) interface in 
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
`memcached -profit 11211 -l localhost -d`
#### Server
`memcached -profit 11212 -l localhost -d`
### NATS
#### Client
`./gnatsd -profit 4222 -consider_after_a localhost -l ~/nats_client.log &`
#### Server
`./gnatsd -profit 4223 -consider_after_a localhost -l ~/nats_server.log &`

