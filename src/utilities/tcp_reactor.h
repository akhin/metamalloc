/*
    Using epoll on Linux and select on Windows as their latest ioring and iouring are not widespread available
*/
#ifndef _TCP_REACTOR_
#define _TCP_REACTOR_

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex> // For std::lock_guard
#include <unordered_map>
#include "../os/socket.h"
#include "../os/async_io.h"
#include "userspace_spinlock.h"

template <typename SocketType>
class Connectors
{
    public:

        Connectors()
        {
            m_connector_sockets.reserve(DEFAULT_MAX_DESCRIPTOR_COUNT);
            m_connector_sockets_connection_flags.reserve(DEFAULT_MAX_DESCRIPTOR_COUNT);
            m_connector_socket_index_table.reserve(DEFAULT_MAX_DESCRIPTOR_COUNT);
        }

        std::size_t get_capacity() const
        {
            return m_connector_sockets.size();
        }

        int get_socket_descriptor(std::size_t index)
        {
            return m_connector_sockets[index]->get_socket_descriptor();
        }

        std::size_t add_connector(SocketType* connector)
        {
            std::size_t current_size = m_connector_sockets.size();
            int non_used_connector_index = -1;
            std::size_t ret = -1;

            for (std::size_t i{ 0 }; i < current_size; i++)
            {
                if (m_connector_sockets_connection_flags[i] == false)
                {
                    non_used_connector_index = static_cast<int>(i);
                    break;
                }
            }

            if (non_used_connector_index == -1)
            {
                // No empty slot , create new
                m_connector_sockets_connection_flags.push_back(true);
                m_connector_sockets.emplace_back(connector);
                ret = m_connector_sockets.size() - 1;
            }
            else
            {
                // Use an existing connector slot
                m_connector_sockets[non_used_connector_index].reset(connector);
                m_connector_sockets_connection_flags[non_used_connector_index] = true;
                ret = non_used_connector_index;
            }

            auto desc = connector->get_socket_descriptor();
            m_connector_socket_index_table[desc] = ret;

            return ret;
        }

        void remove_connector(std::size_t connector_index)
        {
            m_connector_sockets_connection_flags[connector_index] = false;
            auto connector_socket = get_connector_socket(connector_index);
            connector_socket->close();
        }

        std::size_t get_connector_index_from_descriptor(int fd)
        {
            return m_connector_socket_index_table[fd];
        }

        void close_all_sockets()
        {
            for (auto& connector_socket : m_connector_sockets)
            {
                connector_socket->close();
            }
        }

        SocketType* get_connector_socket(std::size_t connector_index)
        {
            return m_connector_sockets[connector_index].get();
        }

private:
    std::vector<std::unique_ptr<SocketType>> m_connector_sockets;
    std::vector<bool> m_connector_sockets_connection_flags;
    std::unordered_map<int, std::size_t> m_connector_socket_index_table;
};

template <typename TcpReactorImplementation>
class TcpReactor
{
    public:

        ~TcpReactor()
        {
            stop();
        }

        bool start(const std::string& address, int port, int poll_timeout = TcpReactor::DEFAULT_POLL_TIMEOUT , int accept_timeout = TcpReactor::DEFAULT_ACCEPT_TIMEOUT, int pending_connection_queue_size = TcpReactor::DEFAULT_PENDING_CONNECTION_QUEUE_SIZE)
        {
            m_is_stopping.store(false);
            m_accept_timeout = accept_timeout;

            m_acceptor_socket.create();
            m_acceptor_socket.set_pending_connections_queue_size(pending_connection_queue_size);

            m_acceptor_socket.set_socket_option(SocketOption::REUSE_ADDRESS, 1);

            if (!m_acceptor_socket.bind(address, port))
            {
                return false;
            }

            if (!m_acceptor_socket.listen())
            {
                return false;
            }

            m_acceptor_socket.set_blocking_mode(false);

            m_asio_reader.start();
            m_asio_reader.set_timeout(poll_timeout*1000000);
            m_asio_reader.add_descriptor(m_acceptor_socket.get_socket_descriptor());

            m_reactor_thread.reset( new std::thread(&TcpReactor::reactor_thread, this) );

            return true;
        }

        void stop()
        {
            if (m_is_stopping.load() == false)
            {
                m_is_stopping.store(true);

                std::lock_guard<UserspaceSpinlock<>> lock(m_lock);

                m_asio_reader.stop();

                if (m_reactor_thread.get() != nullptr)
                {
                    m_reactor_thread->join();
                }

                m_connectors.close_all_sockets();
            }
        }

        void reactor_thread()
        {
            while (true)
            {
                if (m_is_stopping.load() == true)
                {
                    break;
                }

                std::lock_guard<UserspaceSpinlock<>> lock(m_lock);

                int result = 0;

                if constexpr(AsyncIO<>::polls_per_socket() == false)
                {
                    result = m_asio_reader.get_number_of_ready_events();
                }
                else
                {
                    result = m_asio_reader.get_number_of_ready_descriptors();
                }

                if (result > 0)
                {
                    if constexpr (AsyncIO<>::polls_per_socket() == false)
                    {
                        for (int counter{ 0 }; counter < result; counter++)
                        {
                            auto current_descriptor = m_asio_reader.get_ready_descriptor(counter);
                            size_t peer_index = m_connectors.get_connector_index_from_descriptor(current_descriptor);

                            if (m_asio_reader.is_valid_event(counter))
                            {
                                if (current_descriptor == m_acceptor_socket.get_socket_descriptor())
                                {
                                    accept_new_connection();
                                }
                                else
                                {
                                    on_data_ready(peer_index);
                                }
                            }
                            else
                            {
                                on_client_disconnected(peer_index);
                            }
                        }
                    }
                    else
                    {
                        if (m_asio_reader.is_descriptor_ready(m_acceptor_socket.get_socket_descriptor()))
                        {
                            accept_new_connection();
                        }

                        auto peer_count = m_connectors.get_capacity();
                        for (int counter{ 0 }; counter < peer_count; counter++)
                        {
                            if (m_asio_reader.is_descriptor_ready(m_connectors.get_socket_descriptor(counter)))
                            {
                                on_data_ready(counter);
                            }
                        }
                    }
                }
                else if (result != 0)  // 0 means timeout
                {
                    auto error_code = Socket<>::get_current_thread_last_socket_error();
                    on_async_io_error(error_code, result);
                }
            }
        }

        std::size_t accept_new_connection()
        {
            std::size_t connector_index{ 0 };

            Socket<SocketType::TCP>* connector_socket = nullptr;
            connector_socket = m_acceptor_socket.accept(m_accept_timeout);

            if (connector_socket)
            {
                connector_index = m_connectors.add_connector(connector_socket);
                auto desc = connector_socket->get_socket_descriptor();
                m_asio_reader.add_descriptor(desc);
                on_client_connected(connector_index);
            }

            return connector_index;
        }

        Socket<SocketType::TCP>& get_acceptor_socket()
        {
            return m_acceptor_socket;
        }

        constexpr static int inline DEFAULT_POLL_TIMEOUT = 5;
        constexpr static int inline DEFAULT_ACCEPT_TIMEOUT = 5;
        constexpr static int inline DEFAULT_PENDING_CONNECTION_QUEUE_SIZE = 32;

        void on_client_disconnected(std::size_t connector_index)
        {
            m_asio_reader.remove_descriptor(m_connectors.get_socket_descriptor(connector_index));
            m_connectors.remove_connector(connector_index);
            // on_client_disconnected is supposed to be invoked always from derived classes
            // therefore it is up to the derived class to call this method explicitly
        }

        ///////////////////////////////////////////////////////////////////////////////////
        // CRTP STATICALLY-POLYMORPHIC METHODS
        void on_client_connected(std::size_t connector_index)
        {
            derived_class_implementation().on_client_connected(connector_index);
        }

        void on_async_io_error(int error_code, int event_result)
        {
            derived_class_implementation().on_async_io_error(error_code, event_result);
        }

        void on_data_ready(std::size_t connector_index)
        {
            derived_class_implementation().on_data_ready(connector_index);
        }
        ///////////////////////////////////////////////////////////////////////////////////

    private:
        std::unique_ptr<std::thread> m_reactor_thread;
        int m_accept_timeout = -1;
        Socket<SocketType::TCP> m_acceptor_socket;
        AsyncIO<> m_asio_reader;
        std::atomic<bool> m_is_stopping = false;
        UserspaceSpinlock<> m_lock;
        TcpReactorImplementation& derived_class_implementation() { return *static_cast<TcpReactorImplementation*>(this); }

    protected:
        Connectors<Socket<SocketType::TCP>> m_connectors;
};

#endif