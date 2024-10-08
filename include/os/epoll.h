/*
    Uses ( level triggered ) epoll on Linux and select on Windows
    
    If Linux kernel version is >= 5.11 then we will use epoll_pwait2 with nanosecond precision 
    otherwise we will use epoll_wait with millisecond precision
*/
#ifndef _EPOLL_H_
#define _EPOLL_H_


#include <cstddef>

static constexpr std::size_t DEFAULT_EPOLL_MAX_DESCRIPTOR_COUNT = 64;

#ifdef __linux__

#include <cassert>
#include <sys/epoll.h>
#include <unistd.h>

#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#define EPOLL_PWAIT2_AVAILABLE 1
#else
#define EPOLL_PWAIT2_AVAILABLE 0
#endif

template<std::size_t MAX_DESCRIPTOR_COUNT = DEFAULT_EPOLL_MAX_DESCRIPTOR_COUNT>
class Epoll
{
public:
    Epoll()
    {
        m_max_epoll_events = MAX_DESCRIPTOR_COUNT;
        m_epoll_descriptor = epoll_create1(0);
        m_epoll_events = new struct epoll_event[m_max_epoll_events];
    }

    ~Epoll()
    {
        if (m_epoll_descriptor >= 0)
        {
            ::close(m_epoll_descriptor);
        }

        if (m_epoll_events)
        {
            delete[] m_epoll_events;
            m_epoll_events = nullptr;
        }
    }

    static constexpr bool polls_per_socket()
    {
        return false;
    }

    void set_timeout(long nanoseconds)
    {
        #if EPOLL_PWAIT2_AVAILABLE
        m_epoll_wait2_timeout.tv_sec = 0;                       
        m_epoll_wait2_timeout.tv_nsec  = nanoseconds;   
        
        if(m_epoll_wait2_timeout.tv_nsec == 0 )
        {
            m_epoll_wait2_timeout.tv_nsec = 1;
        }
        #else
        m_epoll_timeout_milliseconds = nanoseconds / 1000000;
        
        if( m_epoll_timeout_milliseconds == 0 )
        {
            m_epoll_timeout_milliseconds = 1;
        }
        #endif
    }

    void clear_descriptors()
    {
        if (m_epoll_descriptor >= 0)
        {
            ::close(m_epoll_descriptor);
        }

        m_epoll_descriptor = epoll_create1(0);
    }

    void add_descriptor(int fd)
    {
        struct epoll_event epoll_descriptor;
        epoll_descriptor.data.fd = fd;

        epoll_descriptor.events = EPOLLIN;

        epoll_ctl(m_epoll_descriptor, EPOLL_CTL_ADD, fd, &epoll_descriptor);
    }

    void remove_descriptor(int fd)
    {
        struct epoll_event epoll_descriptor;
        epoll_descriptor.data.fd = fd;
        epoll_descriptor.events = EPOLLIN;

        epoll_ctl(m_epoll_descriptor, EPOLL_CTL_DEL, fd, &epoll_descriptor);
    }

    int get_number_of_ready_events()
    {
        int result{ -1 };
        
        #if EPOLL_PWAIT2_AVAILABLE
        result = ::epoll_pwait2(m_epoll_descriptor, m_epoll_events, m_max_epoll_events, &m_epoll_wait2_timeout, nullptr);
        #else
        result = ::epoll_wait(m_epoll_descriptor, m_epoll_events, m_max_epoll_events, m_epoll_timeout_milliseconds);
        #endif
        return result;
    }

    bool is_valid_event(int index)
    {
        if (m_epoll_events[index].events & EPOLLIN)
        {
            return true;
        }

        return false;
    }

    int get_ready_descriptor(int index)
    {
        int ret{ -1 };
        ret = m_epoll_events[index].data.fd;
        return ret;
    }

    //////////////////////////////////////////////////////////////////////////////
    // COMMON INTERFACE AS GCC'S SUPPORT FOR IF-CONSTEXPR IS NOT AS GOOD AS MSVC
    // EVEN THOUGH THEY WON'T BE CALLED GCC STILL WANTS TO SEE THEM
    int get_number_of_ready_descriptors() { assert(1==0);return 0;}
    bool is_descriptor_ready(int fd) { assert(1==0); return false;}
    //////////////////////////////////////////////////////////////////////////////

private:
    int m_epoll_descriptor = -1;
    struct epoll_event* m_epoll_events = nullptr;
    
    std::size_t m_max_epoll_events = -1;

    #if EPOLL_PWAIT2_AVAILABLE
    struct timespec m_epoll_wait2_timeout;
    #else 
    int m_epoll_timeout_milliseconds = 0;
    #endif
};

#elif _WIN32

#include <Ws2tcpip.h>

template<std::size_t MAX_DESCRIPTOR_COUNT = 0>  // Currently not used in select, just here to conform asynciopoller interface
class Epoll
{
    public:

        Epoll()
        {
            FD_ZERO(&m_query_set);
            FD_ZERO(&m_result_set);
        }

        static constexpr bool polls_per_socket()
        {
            return true;
        }

        void clear_descriptors()
        {
            FD_ZERO(&m_query_set);
        }

        void set_timeout(long nanoseconds)
        {
            m_timeout.tv_sec = 0;
            m_timeout.tv_usec = nanoseconds / 1000;
            
            if( m_timeout.tv_usec == 0 )
            {
                m_timeout.tv_usec = 1;
            }
        }

        void add_descriptor(int fd)
        {
            if (fd > m_max_descriptor)
            {
                m_max_descriptor = fd;
            }

            FD_SET(fd, &m_query_set);
        }

        void remove_descriptor(int fd)
        {
            if (FD_ISSET(fd, &m_query_set))
            {
                FD_CLR(fd, &m_query_set);
            }
        }

        int get_number_of_ready_descriptors()
        {
            m_result_set = m_query_set;
            return ::select(m_max_descriptor + 1, &m_result_set, nullptr, nullptr, &m_timeout);
        }

        bool is_descriptor_ready(int fd)
        {
            bool ret{ false };

            ret = (FD_ISSET(fd, &m_result_set)) ? true : false;

            return ret;
        }

    private :
        int m_max_descriptor = -1;
        struct timeval m_timeout;
        fd_set m_query_set;
        fd_set m_result_set;
};

#endif
#endif