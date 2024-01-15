#ifndef _SOCKET_
#define _SOCKET_

#ifdef __linux__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#elif _WIN32
#pragma comment(lib,"Ws2_32.lib")
// Include order matters in Windows    // VOLTRON_EXCLUDE
// If using Socket class on Windows , ws2tcpip.h should be included before any windows.h inclusion in the entire project // VOLTRON_EXCLUDE
#include <Ws2tcpip.h>
#include <Windows.h>
#pragma warning(disable:4996)
#endif

#include <string>
#include <string_view>
#include <cstring>
#include <cstddef>

enum class SocketType
{
    TCP,
    UDP
};

enum class SocketOption
{
    GET_ERROR_AND_CLEAR,
    REUSE_ADDRESS,
    REUSE_PORT,
    KEEP_ALIVE,
    EXCLUSIVE_ADDRESS,
    RECEIVE_BUFFER_SIZE,
    RECEIVE_BUFFER_TIMEOUT,
    SEND_BUFFER_SIZE,
    SEND_BUFFER_TIMEOUT,
    TCP_ENABLE_CORK,
    TCP_ENABLE_QUICKACK,        // Applies only to Linux , even Nagle is turned off , delayed can cause time loss due in case of lost packages
    TCP_DISABLE_NAGLE,          // Send packets as soon as possible , no need to wait for ACKs or to reach a certain amount of buffer size
    POLLING_INTERVAL,           // SO_BUSY_POLL , specifies time to wait for select to query kernel to know if new data received
    SOCKET_PRIORITY
};

enum class SocketState
{
    DISCONNECTED,
    BOUND,
    CONNECTED,
    LISTENING,
    ACCEPTED
};

class SocketAddress
{
    public:

        void initialise(const std::string_view& address, int port)
        {
            m_port = port;
            m_address = address;

            auto addr = &m_socket_address_struct;

            memset(addr, 0, sizeof(sockaddr_in));
            addr->sin_family = PF_INET;
            addr->sin_port = htons(port);

            if (m_address.size() > 0)
            {
                if (get_address_info(m_address.c_str(), &(addr->sin_addr)) != 0)
                {
                    inet_pton(PF_INET, m_address.c_str(), &(addr->sin_addr));
                }
            }
            else
            {
                addr->sin_addr.s_addr = INADDR_ANY;
            }
        }

        void initialise(struct sockaddr_in* socket_address_struct)
        {
            char ip[50];
            #ifdef __linux__
            inet_ntop(PF_INET, (struct in_addr*)&(socket_address_struct->sin_addr.s_addr), ip, sizeof(ip) - 1);
            #elif _WIN32
            InetNtopA(PF_INET, (struct in_addr*)&(socket_address_struct->sin_addr.s_addr), ip, sizeof(ip) - 1);
            #endif
            m_address = ip;
            m_port = ntohs(socket_address_struct->sin_port);
        }

        const std::string& get_address() const
        {
            return m_address;
        }

        int get_port() const
        {
            return m_port;
        }

        struct sockaddr_in* get_socket_address_struct()
        {
            return &m_socket_address_struct;
        }

    private:
        int m_port = 0;
        std::string m_address;
        struct sockaddr_in m_socket_address_struct;

        static int get_address_info(const char* hostname, struct in_addr* socketAddress)
        {
            struct addrinfo *res{ nullptr };

            int result = getaddrinfo(hostname, nullptr, nullptr, &res);

            if (result == 0)
            {
                memcpy(socketAddress, &((struct sockaddr_in *) res->ai_addr)->sin_addr, sizeof(struct in_addr));
                freeaddrinfo(res);
            }

            return result;
        }
};

template <SocketType socket_type = SocketType::TCP>
class Socket
{
    public :

        // 2 static methods needs to be called at the beginning and end of a program
        static void socket_library_initialise()
        {
            #ifdef _WIN32
            WORD version = MAKEWORD(2, 2);
            WSADATA data;
            WSAStartup(version, &data);
            #endif
        }

        static void socket_library_uninitialise()
        {
            #ifdef _WIN32
            WSACleanup();
            #endif
        }

        Socket():m_socket_descriptor{0}, m_state{ SocketState::DISCONNECTED }, m_pending_connections_queue_size{0}
        {
            socket_library_initialise();
        }

        ~Socket()
        {
            close();
            socket_library_uninitialise();
        }

        bool create()
        {
            if constexpr(socket_type == SocketType::TCP)
            {
                m_socket_descriptor = static_cast<int>(socket(PF_INET, SOCK_STREAM, 0));
            }
            else if constexpr (socket_type == SocketType::UDP)
            {
                m_socket_descriptor = static_cast<int>(socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP));
            }

            if (m_socket_descriptor < 0)
            {
                return false;
            }

            return true;
        }

        void close()
        {
            if (m_state != SocketState::DISCONNECTED)
            {
                if (m_socket_descriptor > 0)
                {
                    #ifdef __linux__
                    ::close(m_socket_descriptor);
                    #elif _WIN32
                    ::closesocket(m_socket_descriptor);
                    #endif
                }
                m_state = SocketState::DISCONNECTED;
            }
        }

        bool bind(const std::string_view& address, int port)
        {
            m_address.initialise(address, port);
            int result = ::bind(m_socket_descriptor, (struct sockaddr*)m_address.get_socket_address_struct(), sizeof(struct sockaddr_in));

            if (result != 0)
            {
                return false;
            }

            m_state = SocketState::BOUND;

            return true;
        }

        bool connect(const std::string_view& address, int port)
        {
            m_address.initialise(address, port);

            if (::connect(m_socket_descriptor, (struct sockaddr*)m_address.get_socket_address_struct(), sizeof(struct sockaddr_in)) != 0)
            {
                return false;
            }

            m_state = SocketState::CONNECTED;
            return true;
        }

        bool connect(const std::string_view& address, int port, int timeout)
        {
            auto blocking_mode_before_the_call = is_in_blocking_mode();
            set_blocking_mode(false);

            bool success{ false };

            success = connect(address, port);

            if (success == false)
            {
                success = select(true, true, timeout);
            }

            set_blocking_mode(blocking_mode_before_the_call);

            if (success == false)
            {
                return false;
            }

            return true;
        }

        Socket* accept(int timeout)
        {
            if (m_state != SocketState::LISTENING && m_state != SocketState::ACCEPTED)
            {
                return nullptr;
            }

            auto blocking_mode_before_the_call = is_in_blocking_mode();
            set_blocking_mode(false);

            bool success{ true };
            int connector_socket_desc{ -1 };
            struct sockaddr_in address;
            socklen_t len = sizeof(address);

            memset(&address, 0, sizeof(address));
            success = select(true, false, timeout);

            if (success)
            {
                connector_socket_desc = static_cast<int>( ::accept(m_socket_descriptor, (struct sockaddr*)&address, &len) );

                if (connector_socket_desc < 0)
                {
                    success = false;
                }
            }

            set_blocking_mode(blocking_mode_before_the_call);

            if (!success)
            {
                return nullptr;
            }

            // It is caller`s responsibility to delete it
            Socket* connector_socket{ nullptr };
            connector_socket = new Socket;
            connector_socket->initialise(connector_socket_desc, &address);
            connector_socket->m_state = SocketState::CONNECTED;

            m_state = SocketState::ACCEPTED;

            return connector_socket;
        }

        void set_pending_connections_queue_size(int value)
        {
            m_pending_connections_queue_size = value;
        }

        void set_blocking_mode(bool blocking_mode)
        {
            #if __linux__
            long arg = fcntl(m_socket_descriptor, F_GETFL, NULL);

            if (blocking_mode)
            {
                arg &= (~O_NONBLOCK);
            }
            else
            {
                arg |= O_NONBLOCK;
            }

            fcntl(m_socket_descriptor, F_SETFL, arg);
            #elif _WIN32
            u_long mode{ 0 };

            if (!blocking_mode)
            {
                mode = 1;
            }

            ioctlsocket(m_socket_descriptor, FIONBIO, &mode);
            #endif
            m_in_blocking_mode = blocking_mode;
        }

        void set_socket_option(SocketOption option, int value)
        {
            int actual_option = get_socket_option_value(option);

            if (!actual_option)
            {
                // Even though called ,not supported on this system, for ex QUICK_ACK for Windows
                return;
            }

            int actual_value = value;
            #if __linux
            setsockopt(m_socket_descriptor, SOL_SOCKET, actual_option, &actual_value, sizeof actual_value);
            #elif _WIN32
            setsockopt(m_socket_descriptor, SOL_SOCKET, actual_option, (char*)&actual_value, sizeof actual_value);
            #endif
        }

        void set_ttl(int ttl)
        {
            #if __linux
            setsockopt(m_socket_descriptor, IPPROTO_IP, IP_TTL, reinterpret_cast<void*>(&ttl), sizeof(ttl));
            #elif _WIN32
            setsockopt(m_socket_descriptor, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof(ttl));
            #endif
        }

        bool listen()
        {
            int result = ::listen(m_socket_descriptor, m_pending_connections_queue_size);

            if (result != 0)
            {
                return false;
            }

            m_state = SocketState::LISTENING;
            return true;
        }

        int get_socket_option(SocketOption option)
        {
            int actual_option = get_socket_option_value(option);
            int ret{ 0 };
            socklen_t len{ 0 };

            #ifdef __linux__
            getsockopt(m_socket_descriptor, SOL_SOCKET, actual_option, (void*)(&ret), &len);
            #elif _WIN32
            getsockopt(m_socket_descriptor, SOL_SOCKET, actual_option, (char*)(&ret), &len);
            #endif

            return ret;
        }

        int get_last_socket_error()
        {
            return get_socket_option(SocketOption::GET_ERROR_AND_CLEAR);
        }

        static int get_current_thread_last_socket_error()
        {
            int ret{ -1 };
            #ifdef __linux__
            ret = errno;
            #elif _WIN32
            ret = WSAGetLastError();
            #endif
            return ret;
        }

        static std::string get_socket_error_as_string(int error_code)
        {
            std::string ret;
            #ifdef __linux__
            ret = strerror(error_code);
            #elif _WIN32
            HMODULE lib = ::LoadLibraryA("WSock32.dll");
            char* temp_string = nullptr;

            FormatMessageA(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
                (LPCVOID)lib, error_code,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&temp_string, 0, NULL);

            if (temp_string)
            {
                ret = temp_string;
                LocalFree(temp_string);
            }

            if (lib)
            {
                ::FreeLibrary(lib);
            }
            #endif
            return ret;
        }

        bool is_connection_lost(int error_code, std::size_t receive_result, bool is_caller_async)
        {
            bool ret{ false };
            #ifdef __linux__
            if (error_code >= 100 && error_code <= 104)
            {
                ret = true;
            }
            #elif _WIN32
            if (error_code == WSAECONNRESET || error_code == WSAECONNABORTED)
            {
                ret = true;
            }
            #endif
            else if(is_caller_async && !receive_result)
            {
                // In case the caller is doing async io with select/poll/epoll/ioring/iouring
                ret = true;
            }

            if (ret == true)
            {
                m_state = SocketState::DISCONNECTED;
            }
            return ret;
        }

        SocketState get_state() const
        {
            return m_state;
        }

        int get_port() const
        {
            return m_address.get_port();
        }

        std::string get_address() const
        {
            return m_address.get_address();
        }

        int get_socket_descriptor() const
        {
            return m_socket_descriptor;
        }

        int receive(char* buffer, std::size_t len, int timeout = 0)
        {
            if (timeout > 0)
            {
                if (select(true, false, timeout) == false)
                {
                    return 0;
                }
            }

            auto result = ::recv(m_socket_descriptor, buffer, static_cast<int>(len), static_cast<int>(0));

            return result;
        }

        int send(const std::string_view& buffer, int timeout = 0)
        {
            return send(buffer.data(), buffer.length(), timeout);
        }

        int send(const char* buffer, std::size_t len, int timeout = 0)
        {
            if (timeout > 0)
            {
                if (select(false, true, timeout) == false)
                {
                    return 0;
                }
            }

            auto result = ::send(m_socket_descriptor, buffer, static_cast<int>(len), static_cast<int>(0)) ;

            return result;
        }

        bool is_in_blocking_mode() const { return m_in_blocking_mode; }

protected:
        // Needed for operations that need timeout
        bool select(bool read, bool write, long timeout)
        {
            fd_set write_set;
            fd_set read_set;
            fd_set* read_set_ptr{ nullptr };
            fd_set* write_set_ptr{ nullptr };

            if (write)
            {
                FD_ZERO(&write_set);
                FD_SET(m_socket_descriptor, &write_set);
                write_set_ptr = &write_set;
            }

            if (read)
            {
                FD_ZERO(&read_set);
                FD_SET(m_socket_descriptor, &read_set);
                read_set_ptr = &read_set;
            }

            struct timeval tv;
            tv.tv_sec = timeout;
            tv.tv_usec = 0;

            // First arg ignored in Windows
            if (::select(m_socket_descriptor + 1, read_set_ptr, write_set_ptr, nullptr, &tv) <= 0)
            {
                return false;
            }

            return true;
        }

private:
    int m_socket_descriptor;
    SocketState m_state;
    bool m_in_blocking_mode = true;
    int m_pending_connections_queue_size;
    SocketAddress m_address;

    // Move ctor deletion
    Socket(Socket&& other) = delete;
    // Move assignment operator deletion
    Socket& operator=(Socket&& other) = delete;

    void initialise(int socket_descriptor, struct sockaddr_in* socket_address)
    {
        m_socket_descriptor = socket_descriptor;
        m_address.initialise(socket_address);
    }

    int get_socket_option_value(SocketOption option)
    {
        int ret{ 0 };

        switch (option)
        {
            case SocketOption::GET_ERROR_AND_CLEAR:
                ret = SO_ERROR;
                break;
            case SocketOption::REUSE_ADDRESS:
                ret = SO_REUSEADDR;
                break;
            case SocketOption::KEEP_ALIVE:
                ret = SO_KEEPALIVE;
                break;
            case SocketOption::EXCLUSIVE_ADDRESS:
                ret = SO_REUSEADDR;
                break;
            case SocketOption::REUSE_PORT:
                #ifdef SO_REUSEPORT
                ret = SO_REUSEPORT;
                #endif
                break;
            case SocketOption::RECEIVE_BUFFER_SIZE:
                ret = SO_RCVBUF;
                break;
            case SocketOption::RECEIVE_BUFFER_TIMEOUT:
                ret = SO_RCVTIMEO;
                break;
            case SocketOption::SEND_BUFFER_SIZE:
                ret = SO_SNDBUF;
                break;
            case SocketOption::SEND_BUFFER_TIMEOUT:
                ret = SO_SNDTIMEO;
                break;
            case SocketOption::TCP_DISABLE_NAGLE:
                ret = TCP_NODELAY;
                break;
            case SocketOption::TCP_ENABLE_QUICKACK:
                #ifdef TCP_QUICKACK
                ret = TCP_QUICKACK;
                #endif
                break;
            case SocketOption::TCP_ENABLE_CORK:
                #ifdef TCP_CORK
                ret = TCP_CORK;
                #endif
                break;
            case SocketOption::SOCKET_PRIORITY:
                #ifdef SO_PRIORITY
                ret = SO_PRIORITY;
                #endif
                break;
            case SocketOption::POLLING_INTERVAL:
                #ifdef SO_BUSY_POLL
                ret = SO_BUSY_POLL;
                #endif
                break;
            default:
                break;
        }

        return ret;
    }
};

#endif