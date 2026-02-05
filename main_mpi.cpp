#include "profiler.h"

#include <iostream>
#include <mpi.h>
#include <fstream>
#include <string>

int main (int argc, char *argv[])
{
    using Profiler::Profiler;
    MPI_Init(&argc, &argv);

    int myid, nproc;
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);

    std::string fname = "profiler_myid_" + std::to_string(myid) + ".txt";
    std::ofstream os;
    os.open(fname);

    auto profiler = Profiler(os);

    profiler.start("hello");
    profiler.stop("hello");
    profiler.start("world");
    profiler.stop("world");
    profiler.display();

    os.close();

    if (myid == 0)
    {
        auto s = profiler.get_profile_string();
        std::cout << s;
    }

    MPI_Finalize();

    return 0;
}
