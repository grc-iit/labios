# Labios

LABIOS, a new, distributed, scalable, and adaptive I/O System. 
LABIOS is the first LAbel-Based I/O System, is fully decoupled, 
and is intended to grow in the intersection of HPC and BigData.

## Installation

We use spack for installing labios
```bash
spack install labios
```

## Deployment

```bash
jarvis labios scaffold default
jarvis labios init
jarvis labios start
jarvis labios stop
```

# TODO LIST

## Future
* Task dependecies (don't care)
* Metadata persistent store (flush at the end)
* Automated server bootstrapping
* Investigate read simulation
* Handle MDM for outstanding operations (data in transit)
** Discuss how important is this issue of decoupled components
** Possible fixes:
*** Invalidation lists + timer expiration to cleanup
*** Intermediate state of data in MDM

## Notes
* Timeout task scheduling. line#66
* check the usleep in task scheduler-> infinite looping
* Aggregating logs
`cat ts_* >> ts.csv`
* Printing correctly
`std::stringstream stream; // #include <sstream> for this
stream << 1 << 2 << 3;
std::cout << stream.str();`
