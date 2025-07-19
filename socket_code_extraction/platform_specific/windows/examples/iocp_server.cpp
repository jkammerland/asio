// Windows IOCP Server Example
// Demonstrates Windows-specific features in ASIO

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <array>
#include <thread>

using asio::ip::tcp;

class iocp_connection : public std::enable_shared_from_this<iocp_connection>
{
public:
  typedef std::shared_ptr<iocp_connection> pointer;

  static pointer create(asio::io_context& io_context)
  {
    return pointer(new iocp_connection(io_context));
  }

  tcp::socket& socket()
  {
    return socket_;
  }

  void start()
  {
    // Demonstrate zero-byte receive optimization
    // This is Windows-specific behavior for stream sockets
    do_read();
  }

private:
  iocp_connection(asio::io_context& io_context)
    : socket_(io_context)
  {
  }

  void do_read()
  {
    auto self(shared_from_this());
    
    // IOCP will complete this operation when data is available
    // The kernel handles the async operation entirely
    socket_.async_read_some(asio::buffer(buffer_),
      [this, self](std::error_code ec, std::size_t length)
      {
        if (!ec)
        {
          std::cout << "Received " << length << " bytes via IOCP\n";
          
          // Echo the data back
          // WSASend will be used internally with IOCP
          do_write(length);
        }
        else
        {
          // Handle Windows-specific errors
          handle_error(ec);
        }
      });
  }

  void do_write(std::size_t length)
  {
    auto self(shared_from_this());
    
    // Async write using IOCP
    asio::async_write(socket_, asio::buffer(buffer_, length),
      [this, self](std::error_code ec, std::size_t /*length*/)
      {
        if (!ec)
        {
          // Continue reading
          do_read();
        }
        else
        {
          handle_error(ec);
        }
      });
  }

  void handle_error(const std::error_code& ec)
  {
    // Map Windows-specific errors
    if (ec.value() == ERROR_NETNAME_DELETED)
    {
      std::cout << "Connection forcibly closed by peer\n";
    }
    else if (ec.value() == ERROR_PORT_UNREACHABLE)
    {
      std::cout << "Port unreachable\n";
    }
    else
    {
      std::cout << "Error: " << ec.message() << " (" << ec.value() << ")\n";
    }
  }

  tcp::socket socket_;
  std::array<char, 8192> buffer_;
};

class iocp_server
{
public:
  iocp_server(asio::io_context& io_context, short port)
    : io_context_(io_context),
      acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    // Set socket options for performance
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    
    // Enable SO_CONDITIONAL_ACCEPT for better accept performance
    // This is Windows-specific
    set_conditional_accept();
    
    start_accept();
  }

private:
  void set_conditional_accept()
  {
    // Windows-specific: SO_CONDITIONAL_ACCEPT
    // Allows application to reject connections before fully accepting
    #ifdef _WIN32
    BOOL enable = TRUE;
    ::setsockopt(acceptor_.native_handle(), 
                 SOL_SOCKET, 
                 SO_CONDITIONAL_ACCEPT,
                 reinterpret_cast<char*>(&enable), 
                 sizeof(enable));
    #endif
  }

  void start_accept()
  {
    iocp_connection::pointer new_connection =
      iocp_connection::create(io_context_);

    // AcceptEx will be used internally on Windows
    // This pre-creates the socket for better performance
    acceptor_.async_accept(new_connection->socket(),
      [this, new_connection](std::error_code ec)
      {
        if (!ec)
        {
          std::cout << "New connection accepted via IOCP\n";
          
          // Set TCP_NODELAY for low latency
          new_connection->socket().set_option(tcp::no_delay(true));
          
          // Start the connection
          new_connection->start();
        }

        // Continue accepting
        start_accept();
      });
  }

  asio::io_context& io_context_;
  tcp::acceptor acceptor_;
};

// Demonstrate IOCP thread pool
void run_iocp_thread_pool(asio::io_context& io_context, std::size_t thread_count)
{
  // Create thread pool
  // IOCP will automatically distribute work across threads
  std::vector<std::thread> threads;
  threads.reserve(thread_count);
  
  std::cout << "Starting IOCP thread pool with " << thread_count << " threads\n";
  
  for (std::size_t i = 0; i < thread_count; ++i)
  {
    threads.emplace_back([&io_context, i]()
    {
      std::cout << "Thread " << i << " entering IOCP loop\n";
      
      // Each thread calls run(), IOCP handles distribution
      io_context.run();
      
      std::cout << "Thread " << i << " exiting IOCP loop\n";
    });
  }
  
  // Wait for all threads
  for (auto& t : threads)
  {
    t.join();
  }
}

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: iocp_server <port>\n";
      return 1;
    }

    // ASIO automatically initializes Winsock on Windows
    asio::io_context io_context;
    
    // Create server
    iocp_server server(io_context, std::atoi(argv[1]));
    
    // Optimal thread count for IOCP is typically the number of CPU cores
    std::size_t thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0)
      thread_count = 2; // Fallback
    
    std::cout << "IOCP server listening on port " << argv[1] << "\n";
    std::cout << "Hardware concurrency: " << thread_count << "\n";
    
    // Run the IOCP event loop with thread pool
    run_iocp_thread_pool(io_context, thread_count);
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

/*
Windows-specific features demonstrated:
1. IOCP-based async operations (WSASend/WSARecv)
2. Thread pool with automatic work distribution
3. SO_CONDITIONAL_ACCEPT for accept optimization
4. Windows error code handling
5. Optimal thread pool sizing for IOCP

To compile on Windows:
cl /EHsc iocp_server.cpp /I<path_to_asio> /link ws2_32.lib mswsock.lib

Performance notes:
- IOCP excels at high connection counts (10K+)
- Thread pool size should match CPU cores
- Zero-byte receives optimize stream socket polling
- AcceptEx provides better accept performance than standard accept()
*/