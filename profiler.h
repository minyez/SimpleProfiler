#pragma once
#include <chrono>
#include <ctime>
#include <memory>
#include <ostream>
#include <string>
#include <sstream>
#include <iomanip>
#ifdef PROFILER_MEMORY_PROF
#if defined(_WIN32)
  #define NOMINMAX
  #include <windows.h>
#elif defined(__APPLE__)
  #include <mach/mach.h>
  #include <mach/mach_host.h>
#elif defined(__FreeBSD__)
  #include <sys/types.h>
  #include <sys/sysctl.h>
#elif defined(_SC_AVPHYS_PAGES) || defined(_SC_PAGESIZE)
  #include <unistd.h>
#endif
#endif

namespace Profiler {

static inline double cpu_time_from_clocks_diff(const std::clock_t& ct_start,
                                 const std::clock_t& ct_end)
{
    return double(ct_end - ct_start) / CLOCKS_PER_SEC;
}

// static std::string get_timestamp()
// {
//     auto now = std::chrono::system_clock::now();
//     auto in_time_t = std::chrono::system_clock::to_time_t(now);
//     auto milliseconds =
//         std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
//     std::stringstream ss;
//     ss << "[" <<std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
//        << '.' << std::setfill('0') << std::setw(3) << milliseconds.count() << "]";
//     return ss.str();
// }

static std::string get_timestamp()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t = system_clock::to_time_t(now);

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char buf[32]; // "[YYYY-mm-dd HH:MM:SS.mmm]" = 25 chars + '\0'
    std::snprintf(buf, sizeof(buf),
                  "[%04d-%02d-%02d %02d:%02d:%02d.%03d]",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms.count()));

    return std::string(buf);
}

#ifdef PROFILER_MEMORY_PROF
// Get node memory in GB
static int get_node_free_mem(double &free_mem)
{
    int retcode = 1;
    // value in KB unit
    long value_kb = 0L;
    unsigned long long bytes = 0ULL;

#if defined(__linux__)
    char line[1024];
    FILE *fp = nullptr;

    if ((fp = fopen("/proc/meminfo", "r")) != nullptr)
    {
        while (std::fgets(line, sizeof(line), fp))
        {
            long v = 0L;
            if (std::sscanf(line, "MemAvailable: %ld kB", &v) == 1)
            {
                value_kb = v;
                retcode = 0;
                break;
            }
        }
        fclose(fp);
    }
    if (retcode == 0)
        bytes = static_cast<unsigned long long>(value_kb) * 1000ULL;

#elif defined(__APPLE__)
    // macOS: estimate "available" as (free + inactive + speculative) * page_size
    // This is closer to what Activity Monitor calls "Available" than just free_count.
    mach_port_t host = mach_host_self();

    vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) != KERN_SUCCESS)
    {
        free_mem = 0.0;
        return 1;
    }

    vm_statistics64_data_t vmstat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vmstat),
                          &count) != KERN_SUCCESS)
    {
        free_mem = 0.0;
        return 1;
    }

    unsigned long long avail_pages =
        static_cast<unsigned long long>(vmstat.free_count) +
        static_cast<unsigned long long>(vmstat.inactive_count) +
        static_cast<unsigned long long>(vmstat.speculative_count);

    bytes = avail_pages * static_cast<unsigned long long>(page_size);
    retcode = 0;

#elif defined(_WIN32)
    // Windows: ullAvailPhys is available physical memory in bytes
    MEMORYSTATUSEX st;
    std::memset(&st, 0, sizeof(st));
    st.dwLength = sizeof(st);

    if (GlobalMemoryStatusEx(&st)) {
        bytes = static_cast<unsigned long long>(st.ullAvailPhys);
        retcode = 0;
    }

#elif defined(__FreeBSD__)
    // FreeBSD: available pages ~= v_free_count (there are other tunables, but keep it simple)
    // vm.stats.vm.v_page_size (bytes), vm.stats.vm.v_free_count (pages)
    u_int page_size = 0;
    u_long free_count = 0;
    size_t len = 0;

    len = sizeof(page_size);
    if (sysctlbyname("vm.stats.vm.v_page_size", &page_size, &len, nullptr, 0) != 0)
    {
        free_mem = 0.0;
        return 1;
    }

    len = sizeof(free_count);
    if (sysctlbyname("vm.stats.vm.v_free_count", &free_count, &len, nullptr, 0) != 0)
    {
        free_mem = 0.0;
        return 1;
    }

    bytes = static_cast<unsigned long long>(free_count) *
            static_cast<unsigned long long>(page_size);
    retcode = 0;

#elif defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGESIZE)
    // POSIX-ish fallback: available physical pages * page size
    // Note: on some systems this can be less "smart" than Linux MemAvailable.
    long av_pages = sysconf(_SC_AVPHYS_PAGES);
    long pg_size  = sysconf(_SC_PAGESIZE);

    if (av_pages > 0 && pg_size > 0)
    {
        bytes = static_cast<unsigned long long>(av_pages) *
                static_cast<unsigned long long>(pg_size);
        retcode = 0;
    }
#endif

    // Convert bytes -> GB (decimal)
    free_mem = static_cast<double>(bytes) * 1.e-9;
    return retcode;
}
#endif

static std::string banner(char c, int n)
{
    std::string s = "";
    while(n--) s += c;
    return s;
}

//! A simple profiler object to record timing of code snippet runs in the program.
class Profiler
{
private:
    //! Class to track timing of a particular part of code
    class Timer
    {
    public:
        //! the number of timer calls
        size_t ncalls;
        //! clock when the timer is started
        std::clock_t clock_start;
        //! wall time when the timer is started
        std::chrono::time_point<std::chrono::system_clock> wt_start;
        //! accumulated cpu time
        double cpu_time_accu;
        //! accumulated wall time, i.e. elapsed time
        double wall_time_accu;
        //! cpu time during last call
        double cpu_time_last;
        //! wall time during last call
        double wall_time_last;
        //! Timer name
        std::string name;
        //! Side note for the timer, not used as timer identification
        std::string note;

        std::shared_ptr<Timer> parent;
        std::shared_ptr<Timer> prev;
        std::shared_ptr<Timer> next;
        // First child
        std::shared_ptr<Timer> child;

        Timer(const std::string &tname, const std::string &tnote)
            : ncalls(0), clock_start(0), wt_start(), cpu_time_accu(0),
              wall_time_accu(0), cpu_time_last(0), wall_time_last(0),
              name(tname), note(tnote), parent(nullptr), prev(nullptr),
              next(nullptr), child(nullptr) {}

        //! start the timer
        void start() noexcept
        {
            if(is_on())
                stop();
            ncalls++;
            clock_start = clock();
            wt_start = std::chrono::system_clock::now();
            cpu_time_last = 0.0;
            wall_time_last = 0.0;
            // librpa_int::global::lib_printf("start: %zu %zu %f\n", ncalls, clock_start, wt_start);
        }

        //! stop the timer and record the timing
        void stop() noexcept
        {
            if(!is_on()) return;
            cpu_time_accu += (cpu_time_last = cpu_time_from_clocks_diff(clock_start, clock()));
            const auto wt_end = std::chrono::system_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(wt_end - wt_start);
            wall_time_accu += (wall_time_last = duration.count() * 1e-6);
            // reset
            wt_start = {};
            clock_start = 0;
        }

        bool is_on() const { return clock_start != 0; };
    };

    std::ostream *p_os;
    std::shared_ptr<Timer> root;
    std::shared_ptr<Timer> current;

    // Find child timer with timer name
    std::shared_ptr<Timer> search_timer_in_hierarchy(std::shared_ptr<Timer> timer, const std::string& tname)
    {
        if (!timer) return nullptr;
        if (timer->name == tname) return timer;

        // Recursively check child timers
        if (timer->child)
        {
            auto found_timer = search_timer_in_hierarchy(timer->child, tname);
            if (found_timer)
            {
                return found_timer;
            }
        }

        // Check the next sibling timer
        if (timer->next)
        {
            return search_timer_in_hierarchy(timer->next, tname);
        }

        return nullptr;
    }

    std::shared_ptr<Timer> find_timer_in_hierarchy(const std::string &tname)
    {
        return search_timer_in_hierarchy(current, tname);
    }

    std::string get_profile_string_of_timer(std::shared_ptr<Timer> timer, const int level, const int verbose)
    {
        std::ostringstream ss;
        // std::string indent(2 * level, ' ');
        std::string indent_s(this->indent * level, ' ');
        const auto note = indent_s + (timer->note == "" ? timer->name : timer->note);
        std::ostringstream cstr_cputime, cstr_walltime;
        cstr_cputime << std::fixed << std::setprecision(4) << timer->cpu_time_accu;
        cstr_walltime << std::fixed << std::setprecision(4) << timer->wall_time_accu;

        // Print self
        ss << std::left;
        ss << std::setw(49) << note << " " << std::setw(12) << timer->ncalls << " "
            << std::setw(18) << (indent_s + cstr_cputime.str()) << " "
            << std::setw(18) << (indent_s + cstr_walltime.str()) << "\n";
        // Print child, then sibling
        if (timer->child && verbose > level)
            ss << get_profile_string_of_timer(timer->child, level + 1, verbose);
        if (timer->next && verbose >= level)
            ss << get_profile_string_of_timer(timer->next, level, verbose);
        return ss.str();
    }

public:
    //! Indent for printing final statistics
    unsigned int indent;

    Profiler() : p_os(nullptr), root(nullptr), current(nullptr), indent(1) {};
    Profiler(std::ostream &os_in)
        : p_os(&os_in), root(nullptr), current(nullptr), indent(1) {};

    //! Add a timer
    void add(const std::string &tname, const std::string &tnote = "") noexcept
    {
        auto new_timer = std::make_shared<Timer>(tname, tnote);

        if (!root)
        {
            root = new_timer;
        }
        else
        {
            if (current)
            {
                if (current->child)
                {
                    auto sibling = current->child;
                    while (sibling->next) sibling = sibling->next;
                    sibling->next = new_timer;
                    new_timer->prev = sibling;
                }
                else
                {
                    current->child = new_timer;
                }
                new_timer->parent = current;
            }
        }

        current = new_timer;
    }

    //! Start a timer. If the timer is not added before, add it.
    void start(const std::string &tname, const std::string &tnote = "") noexcept
    {
        // if (omp_get_thread_num() != 0) return;
        auto timer = find_timer_in_hierarchy(tname);
        // create a new timer if it is not found, otherwise we move to that timer and start
        if (!timer)
        {
            add(tname, tnote);
        }
        else
        {
            current = timer;
        }
        if (p_os) *p_os << get_timestamp() <<" Timer start: " << tname;
#ifdef PROFILER_MEMORY_PROF
        double free_mem_gb;
        get_node_free_mem(free_mem_gb);
        if (p_os) *p_os << ". Free memory on node [GB]: " << free_mem_gb;
#endif
        if (p_os) *p_os << std::endl;
        current->start();
    }

    //! Stop a timer and record the timing
    void stop(const std::string &tname) noexcept
    {
        // if (omp_get_thread_num() != 0) return;
        if (current)
        {
            // Check if the current timer matches the given timer name
            if (current->name == tname)
            {
                current->stop();
                current = current->parent;
                if (p_os) *p_os << get_timestamp() <<" Timer stop:  " << tname;
#ifdef PROFILER_MEMORY_PROF
                double free_mem_gb;
                get_node_free_mem(free_mem_gb);
                if (p_os) *p_os << ". Free memory on node [GB]: " << free_mem_gb;
#endif
                if (p_os) *p_os << std::endl;
            }
            else
            {
                if (p_os)
                    *p_os << "Warning: Attempting to stop timer '" << tname
                          << "' but current active timer is '" << current->name << "'" << std::endl;
            }
        }
        else
        {
            if (p_os) *p_os << "Warning: No timer is currently active" << std::endl;
        }
    }

    //! Get cpu time of last call of timer
    double get_cpu_time_last(const std::string &tname) noexcept
    {
        auto timer = this->find_timer_in_hierarchy(tname);
        if (timer)
            return timer->cpu_time_last;
        return -1.0;
    }

    //! Get wall time of last call of timer
    double get_wall_time_last(const std::string &tname) noexcept
    {
        auto timer = this->find_timer_in_hierarchy(tname);
        if (timer)
            return timer->wall_time_last;
        return 0.0;
    }

    std::string get_profile_string(const int verbose = 99) noexcept
    {
        std::ostringstream output;
        output << std::left;

        output << banner('-', 100) << "\n";
        output << std::setw(49) << "Entry" << " " << std::setw(12) << "#calls" << " "
            << std::setw(18) << "CPU time (s)" << " " << std::setw(18) << "Wall time (s)" << "\n";
        output << banner('-', 100) << "\n";
        output << get_profile_string_of_timer(root, 0, verbose);
        output << banner('-', 100) << "\n";

        return output.str();
    }

    //! Display the current profiling result
    void display(const int verbose = 99) noexcept
    {
        if (p_os) *p_os << this->get_profile_string(verbose);
    }

};

}
