/* 
    CPR : Connection per request

    ITS PARSER ONLY HANDLES CONNECTION-PER-REQUEST MODEL WHERE THERE WILL BE ONE UNIQUE TCP CONNECTION PER HTTP REQUEST
    THEREFORE THE PARSER DOES NOT CONSIDER INCOMPLETE BYTES 
    
    USE CASE :   IN SIMPLE HTTP APPS AJAX'S XMLHttpRequest FOR POST REQUEST , YOU CAN NOT REUSE AN EXISTING TCP CONNECTION
                 Https://stackoverflow.com/questions/32505128/how-to-make-xmlhttprequest-reuse-a-tcp-connection
*/
#ifndef __HTTP_CPR_REACTOR_H__
#define __HTTP_CPR_REACTOR_H__

#include <cstddef>
#include <cassert>
#include <string>
#include <string_view>
#include <array>
#include "../compiler/unused.h"
#include "../os/epoll.h"
#include "tcp_reactor.h"

namespace HTTPConstants
{
    static inline const char* VERB_POST = "POST";
    static inline const char* VERB_GET = "GET";
    static inline const char* VERB_PUT = "PUT";
    static inline const char* VERB_DEL = "DEL";
}

enum class HTTPVerb
{
    NONE, // PARSE ERRORS OR UNHANDLED HTTP VERBS
    POST, // CREATE
    GET,  // READ
    PUT,  // UPDATE
    DEL   // DELETE
};

class HttpRequest
{
    public:

        void set_verb(HTTPVerb verb) { m_verb = verb; }
        void set_body(const std::string_view& body) { m_body = body; }

        const std::string body() const { return m_body; }
        const HTTPVerb verb() const { return m_verb; }
    private:
        HTTPVerb m_verb = HTTPVerb::NONE;
        std::string m_body;
};

class HttpResponse
{
    public:
        void set_response_code_with_text(std::string code_with_text) { m_response_code_with_text = code_with_text; }
        void set_body(const std::string_view& body) { m_body = body; }
        void set_connection_alive(bool b) { m_connection_alive = b; }
        void set_content_type(const std::string_view& content_type) { m_content_type = content_type; }
        void set_http_version(const std::string_view& version) { m_http_version = version; }

        const std::string get_as_text()
        {
            std::string ret;
            ret += "HTTP/" + m_http_version + " ";
            ret += m_response_code_with_text;
            ret += "\nConnection: " + (m_connection_alive ? std::string("keep-alive") : std::string("Closed"));
            ret += "\nContent-Type: " + m_content_type;
            ret += "\nContent length: " + std::to_string(m_body.size());
            ret += "\n\n";
            ret += m_body;
            return ret;
        }

    private:
        std::string m_response_code_with_text = "";
        bool m_connection_alive = true;
        std::string m_http_version = "1.1";
        std::string m_content_type = "text/html";
        std::string m_body = "";
};

template <typename HTTPCPRReactorImplementation>
class HTTPCPRReactor : public  TcpReactor<HTTPCPRReactor<HTTPCPRReactorImplementation>, Epoll<>>
{
public:

    HTTPCPRReactor()
    {
        m_cache.reserve(RECEIVE_SIZE);
    }

    ~HTTPCPRReactor() {}
    HTTPCPRReactor(const HTTPCPRReactor& other) = delete;
    HTTPCPRReactor& operator= (const HTTPCPRReactor& other) = delete;
    HTTPCPRReactor(HTTPCPRReactor&& other) = delete;
    HTTPCPRReactor& operator=(HTTPCPRReactor&& other) = delete;

    void on_data_ready(std::size_t peer_index)
    {
        auto peer_socket = this->m_connectors.get_connector_socket(peer_index);
        std::size_t received_bytes{ 0 };

        char read_buffer[RECEIVE_SIZE] = { 0 };
        auto read = peer_socket->receive(read_buffer, RECEIVE_SIZE);

        if (read > 0 && read <= static_cast<int>(RECEIVE_SIZE))
        {
            received_bytes += read;
            read_buffer[read] = '\0';
            m_cache += read_buffer;
        }
        else 
        {
            auto error = Socket<>::get_current_thread_last_socket_error();
            
            if( read == 0)
            {
                on_client_disconnected(peer_index);
            }
            else if (peer_socket->is_connection_lost_during_receive(error))
            {
                on_client_disconnected(peer_index);
            }           
            else if (error != 0)
            {
                this->on_socket_error(error, read);
            }
        }

        if (received_bytes > 0)
        {
            std::array<HttpRequest, MAX_INCOMING_HTTP_REQUEST_COUNT> http_requests;
            std::size_t http_request_count = 0;

            if (parse_http_request(http_requests, http_request_count))
            {
                for(std::size_t i =0; i< http_request_count; i++)
                {
                    switch (http_requests[i].verb())
                    {
                    case HTTPVerb::NONE:
                        assert(0 == 1); // INVALID BUFFER
                        break;
                    case HTTPVerb::GET:
                        on_http_get_request(http_requests[i], peer_socket);
                        break;
                    case HTTPVerb::PUT:
                        on_http_put_request(http_requests[i], peer_socket);
                        break;
                    case HTTPVerb::POST:
                        on_http_post_request(http_requests[i], peer_socket);
                        break;
                    case HTTPVerb::DEL:
                        on_http_delete_request(http_requests[i], peer_socket);
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        peer_socket->close();
        on_client_disconnected(peer_index);
    }

    void on_http_get_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_get_request(http_request, connector_socket);
    }

    void on_http_post_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_post_request(http_request, connector_socket);
    }

    void on_http_put_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_put_request(http_request, connector_socket);
    }

    void on_http_delete_request(const HttpRequest& http_request, Socket<SocketType::TCP>* connector_socket)
    {
        static_cast<HTTPCPRReactorImplementation*>(this)->on_http_delete_request(http_request, connector_socket);
    }

    void on_client_connected(std::size_t peer_index)
    {
        UNUSED(peer_index);
    }

    void on_client_disconnected(std::size_t peer_index)
    {
        TcpReactor<HTTPCPRReactor, Epoll<>>::on_client_disconnected(peer_index);
    }

    void on_async_io_error(int error_code, int event_result)
    {
        UNUSED(error_code);
        UNUSED(event_result);
    }

    void on_socket_error(int error_code, int event_result)
    {
        UNUSED(error_code);
        UNUSED(event_result);
    }

private:
    static inline constexpr std::size_t RECEIVE_SIZE = 4096;
    static inline constexpr std::size_t MAX_INCOMING_HTTP_REQUEST_COUNT = 32;
    std::string m_cache;
    std::size_t m_cache_index = 0;

private:

    /*
        Example GET request :

            GET / HTTP/1.1
            Host: localhost:555
            Connection: keep-alive
            sec-ch-ua: "Chromium";v="118", "Google Chrome";v="118", "Not=A?Brand";v="99"
            ...

        Example POST request :

            POST /your_server_url_here HTTP/1.1
            Host: localhost:555
            Connection: keep-alive
            Content-Length: 6
            sec-ch-ua: "Chromium";v="118", "Google Chrome";v="118", "Not=A?Brand";v="99"
            ...
            Accept - Language : en - GB, en - US; q = 0.9, en; q = 0.8, tr; q = 0.7

            data = A
    */
    bool parse_http_request(std::array<HttpRequest, MAX_INCOMING_HTTP_REQUEST_COUNT>& http_requests, std::size_t& http_request_count)
    {
        std::size_t num_processed_characters{ 0 };
        http_request_count = 0;

        while (true)
        {
            char* buffer_start = &m_cache[0] + m_cache_index + num_processed_characters;
            std::size_t buffer_length = m_cache.length() - m_cache_index - num_processed_characters;
            std::string_view buffer(buffer_start, buffer_length);

            if (buffer.find("Host: ") == std::string_view::npos) // It is mandatory for all HTTP versions to include 'Host' attribute
            {
                break;
            }

            HttpRequest request;

            // EXTRACT VERB
            auto verb_line_end_position = buffer.find("\r\n");

            if (verb_line_end_position == std::string_view::npos)
            {
                return false;
            }

            std::string_view verb_line(buffer.data(), verb_line_end_position);

            auto verb_line_verb_end_position = verb_line.find(' ');

            std::string_view verb(verb_line.data(), verb_line_verb_end_position);

            if (verb == HTTPConstants::VERB_GET)
            {
                request.set_verb(HTTPVerb::GET);
            }
            else if (verb == HTTPConstants::VERB_POST)
            {
                request.set_verb(HTTPVerb::POST);
            }
            else if (verb == HTTPConstants::VERB_PUT)
            {
                request.set_verb(HTTPVerb::PUT);
            }
            else if (verb == HTTPConstants::VERB_DEL)
            {
                request.set_verb(HTTPVerb::DEL);
            }

            // EXTRACT BODY
            constexpr std::size_t double_newline_length = 4;
            auto body_start_position = buffer.find("\r\n\r\n");
            std::string_view body(buffer.data() + body_start_position + double_newline_length, buffer_length - body_start_position - double_newline_length);
            request.set_body(body);

            http_requests[http_request_count] = request;
            http_request_count++;
            num_processed_characters += (body_start_position + double_newline_length);
        }

        m_cache_index += (m_cache.length() - m_cache_index);

        return true;
    }
};

#endif