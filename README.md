---
title: SimpleProfiler
---

A simple (naive) header-only profiler for C++, which only requires standard libraries.

## Usage

Please refer to `main.cpp` for example of usage and `main_mpi.cpp` to use it in MPI environment.

To compile and run the examples:

```bash
export CXX=g++
$CXX main.cpp -o demo_profiler.exe
./demo_profiler.exe

# Optionally with memory check
$CXX -DPROFILER_MEMORY_PROF main.cpp -o demo_profiler_with_mem.exe
./demo_profiler_with_mem.exe
```

```bash
export MPICXX=mpicxx
$MPICXX -DPROFILER_MEMORY_PROF main_mpi.cpp -o demo_profiler_mpi_with_mem.exe
mpirun -np 4 demo_profiler_mpi_with_mem.exe
```

## Note

The methods of `Profiler::Profiler` class is not thread-safe.
Please be careful when using it with threading.
