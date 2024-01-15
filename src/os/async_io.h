/*
    Uses epoll on Linux and select on Windows
    Could use iouring on Linux and ioring on Windows however only latest OSs support them
    so sticking with what is widely available
*/
#ifndef _ASYNC_IO_
#define _ASYNC_IO_


#include <cstddef>

static constexpr std::size_t DEFAULT_MAX_DESCRIPTOR_COUNT = 64;

#ifdef __linux__

#include <cassert>
#include <sys/epoll.h>
#include <unistd.h>

enum class EpollMode
{
    LEVEL_TRIGGERED,
    EDGE_TRIGGERED
};

template<std::size_t MAX_DESCRIPTOR_COUNT = DEFAULT_MAX_DESCRIPTOR_COUNT>
class AsyncIO
{
public:
    AsyncIO()
    {
    }

    ~AsyncIO()
    {
        stop();
    }

    static constexpr bool polls_per_socket()
    {
        return false;
    }

    void set_timeout(long microseconds)
    {
        m_epoll_timeout = microseconds / 1000;
    }

    void start(std::size_t max_epoll_events= MAX_DESCRIPTOR_COUNT, EpollMode mode = EpollMode::LEVEL_TRIGGERED)
    {
        m_max_epoll_events = max_epoll_events;
        m_epoll_mode = mode;
        m_epoll_descriptor = epoll_create1(0);
        m_epoll_events = new struct epoll_event[m_max_epoll_events];
    }

    void stop()
    {
        if(m_epoll_descriptor>=0)
        {
            ::close(m_epoll_descriptor);
        }

        if (m_epoll_events)
        {
            delete[] m_epoll_events;
            m_epoll_events = nullptr; // delete[] does not null
        }
    }

    void add_descriptor(int fd)
    {
        struct epoll_event epoll_descriptor;
        epoll_descriptor.data.fd = fd;

        epoll_descriptor.events = EPOLLIN;

        if (m_epoll_mode == EpollMode::EDGE_TRIGGERED)
        {
            epoll_descriptor.events |= EPOLLET;
        }

        epoll_ctl(m_epoll_descriptor, EPOLL_CTL_ADD, fd, &epoll_descriptor);
    }

    void remove_descriptor(int fd)
    {
        struct epoll_event epoll_descriptor;
        epoll_descriptor.data.fd = fd;
        epoll_descriptor.events = EPOLLIN;

        if (m_epoll_mode == EpollMode::EDGE_TRIGGERED)
        {
            epoll_descriptor.events |= EPOLLET;
        }

        epoll_ctl(m_epoll_descriptor, EPOLL_CTL_DEL, fd, &epoll_descriptor);
    }

    int get_number_of_ready_events()
    {
        int result{ -1 };
        result = ::epoll_wait(m_epoll_descriptor, m_epoll_events, m_max_epoll_events, m_epoll_timeout);
        return result;
    }

    bool is_valid_event(int index)
    {
        if ((m_epoll_events[index].events & EPOLLERR) || (m_epoll_events[index].events & EPOLLHUP) || (!(m_epoll_events[index].events & EPOLLIN)))
        {
            return false;
        }

        return true;
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
    EpollMode m_epoll_mode = EpollMode::LEVEL_TRIGGERED;
    int m_epoll_descriptor = -1;
    struct epoll_event* m_epoll_events = nullptr;
    int m_epoll_timeout = 0;
    std::size_t m_max_epoll_events = -1;
};

#elif _WIN32

#include <algorithm>
#include <vector>
#include <Ws2tcpip.h>

template<std::size_t MAX_DESCRIPTOR_COUNT = DEFAULT_MAX_DESCRIPTOR_COUNT>
class AsyncIO
{
    public:

        AsyncIO()
        {
            m_descriptors.reserve(MAX_DESCRIPTOR_COUNT);
            reset();
        }

        static constexpr bool polls_per_socket()
        {
            return true;
        }

        void start()
        {
            reset();
        }

        void stop()
        {
            reset();
        }

        void set_timeout(long microseconds)
        {
            m_timeout.tv_sec = (microseconds / 1000000);
            m_timeout.tv_usec = 0;
        }

        void add_descriptor(int fd)
        {
            m_descriptors.push_back(fd);
            if (fd > m_max_descriptor)
            {
                m_max_descriptor = fd;
            }
        }

        void remove_descriptor(int fd)
        {
            if (std::find(m_descriptors.begin(), m_descriptors.end(), fd) != m_descriptors.end()) // Protection against double removal
            {
                FD_CLR(fd, &m_clients_read_set);
                m_descriptors.erase(std::remove(m_descriptors.begin(), m_descriptors.end(), fd));
            }
        }

        int get_number_of_ready_descriptors()
        {
            int ret{ -1 };

            reset();

            for(const auto& descriptor : m_descriptors)
            {
                FD_SET(descriptor, &m_clients_read_set);
            }

            ret = ::select(m_max_descriptor + 1, &m_clients_read_set, nullptr, nullptr, &m_timeout);

            return ret;
        }

        bool is_descriptor_ready(int fd)
        {
            bool ret{ false };

            ret = (FD_ISSET(fd, &m_clients_read_set)) ? true : false;

            return ret;
        }


    private :
        std::vector<int> m_descriptors;
        int m_max_descriptor = -1;
        struct timeval m_timeout;
        fd_set m_clients_read_set;



        void reset()
        {
            FD_ZERO(&m_clients_read_set);
        }
};

#endif
#endif