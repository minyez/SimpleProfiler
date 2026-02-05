#include "profiler.h"

#include <iostream>

int main (int argc, char *argv[])
{
    using Profiler::Profiler;

    // A verbose profiler that writes timestamp (and also memory if applicable)
    // to output stream when start/stop.
    auto profiler = Profiler(std::cout);

    // Start a top-level timer, with an optional timer note
    profiler.start("hello", "Say Hello to");

    // Start a level-2 timer
    profiler.start("World");
    // Stop the timer
    profiler.stop("World");

    // Start another level-2 timer
    profiler.start("You");
    profiler.stop("You");

    // Stop the top-level timer, main work finished
    profiler.stop("hello");

    std::cout << "Statistics from 'profiler'" << std::endl;
    profiler.display();
    std::cout << std::endl;

    // A silent profiler that does not write internally
    auto profiler_silent = Profiler();
    profiler_silent.start("test_silent", "Test silent");
    profiler_silent.start("test_1");
    profiler_silent.stop("test_1");
    profiler_silent.start("test_2");
    profiler_silent.stop("test_2");
    profiler_silent.stop("test_silent");
    // Adjust indentation before printing
    profiler_silent.indent = 2;
    // You can still get the profiling summary by get_profile_string
    auto s = profiler_silent.get_profile_string();
    std::cout << "Statistics from 'profiler_silent'" << std::endl;
    std::cout << s;

    return 0;
}
