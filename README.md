# Labios

LABIOS, a new, distributed, scalable, and adaptive I/O System.
LABIOS is the first LAbel-Based I/O System, is fully decoupled,
and is intended to grow in the intersection of HPC and BigData.

NOTE: We are restructuring this repo to support the Chimaera runtime.
Below are the manual build instructions

## Building
```
spack install iowarp+nocompile+vfd+mpiio
cd tasks
mkdir build
cd build
cmake ../
make -j32 install
```
