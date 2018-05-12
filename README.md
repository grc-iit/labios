# README #

##TODO CODE
* IDs everywhere
* Debugged with many cases for logical bugs
* Metadata updates and creation
* Task scheduling with greedy
* code cleanup
* add daemon for SS and WS
##TODO INVESTIGATION
* Investigate worker suspension
* Investigate read simulation
* Automated server bootstrapping
* Cluster environment: 1 Lib, 1 TaskSched, 2 worker


##Setup Porus
### Memcached
#### Client
`memcached -p 11211 -l localhost -d  -I 4M`
#### Server
`memcached -p 11212 -l localhost -d  -I 4M`
### NATS
#### Client
`gnatsd -p 4222 -a localhost -DV -m 8222 -l ~/nats_client.log &`
#### Server
`gnatsd -p 4223 -a localhost -l -DV -m 8223 -l ~/nats_server.log &`




