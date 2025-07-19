// Windows-Specific ASIO Features Example
// Demonstrates features unique to Windows IOCP implementation

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <chrono>

#ifdef _WIN32
#include <mswsock.h>  // For Windows-specific socket functions
#endif

using asio::ip::tcp;

class windows_socket_demo
{
public:
  windows_socket_demo(asio::io_context& io_context)
    : io_context_(io_context),
      socket_(io_context),
      acceptor_(io_context)
  {
  }

  void demonstrate_features()
  {
    std::cout << "=== Windows-Specific ASIO Features Demo ===\n\n";
    
    // 1. Demonstrate Winsock initialization
    demonstrate_winsock_init();
    
    // 2. Demonstrate IOCP association
    demonstrate_iocp_association();
    
    // 3. Demonstrate ConnectEx usage
    demonstrate_connectex();
    
    // 4. Demonstrate zero-byte receives
    demonstrate_zero_byte_receive();
    
    // 5. Demonstrate immediate completion
    demonstrate_immediate_completion();
    
    // 6. Demonstrate cancellation
    demonstrate_cancellation();
  }

private:
  void demonstrate_winsock_init()
  {
    std::cout << "1. Winsock Initialization\n";
    std::cout << "   - ASIO automatically initializes Winsock 2.2\n";
    
    #ifdef _WIN32
    // Check Winsock version
    WSADATA wsaData;
    int result = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result == 0)
    {
      std::cout << "   - Winsock version: " 
                << int(LOBYTE(wsaData.wVersion)) << "."
                << int(HIBYTE(wsaData.wVersion)) << "\n";
      std::cout << "   - High version: "
                << int(LOBYTE(wsaData.wHighVersion)) << "."
                << int(HIBYTE(wsaData.wHighVersion)) << "\n";
      ::WSACleanup();
    }
    #endif
    
    std::cout << "\n";
  }

  void demonstrate_iocp_association()
  {
    std::cout << "2. IOCP Socket Association\n";
    
    try
    {
      // Open a socket
      socket_.open(tcp::v4());
      
      #ifdef _WIN32
      // On Windows, the socket is automatically associated with IOCP
      std::cout << "   - Socket created with WSA_FLAG_OVERLAPPED\n";
      std::cout << "   - Automatically associated with io_context's IOCP\n";
      std::cout << "   - Native handle: " << socket_.native_handle() << "\n";
      #endif
      
      socket_.close();
    }
    catch (std::exception& e)
    {
      std::cout << "   - Error: " << e.what() << "\n";
    }
    
    std::cout << "\n";
  }

  void demonstrate_connectex()
  {
    std::cout << "3. ConnectEx Usage\n";
    
    #ifdef _WIN32
    try
    {
      socket_.open(tcp::v4());
      
      // ASIO uses ConnectEx internally for async_connect
      std::cout << "   - async_connect uses ConnectEx when available\n";
      std::cout << "   - Provides true async connect (no thread blocking)\n";
      std::cout << "   - Can send data with connection request\n";
      
      // Check if ConnectEx is available
      GUID connectex_guid = WSAID_CONNECTEX;
      LPFN_CONNECTEX connectex_fn = nullptr;
      DWORD bytes = 0;
      
      int result = ::WSAIoctl(socket_.native_handle(),
                             SIO_GET_EXTENSION_FUNCTION_POINTER,
                             &connectex_guid, sizeof(connectex_guid),
                             &connectex_fn, sizeof(connectex_fn),
                             &bytes, nullptr, nullptr);
      
      if (result == 0)
      {
        std::cout << "   - ConnectEx is available on this system\n";
      }
      else
      {
        std::cout << "   - ConnectEx not available, using reactor fallback\n";
      }
      
      socket_.close();
    }
    catch (std::exception& e)
    {
      std::cout << "   - Error: " << e.what() << "\n";
    }
    #else
    std::cout << "   - Not available on non-Windows platforms\n";
    #endif
    
    std::cout << "\n";
  }

  void demonstrate_zero_byte_receive()
  {
    std::cout << "4. Zero-byte Receive Optimization\n";
    
    try
    {
      // Set up a listening socket
      acceptor_.open(tcp::v4());
      acceptor_.bind(tcp::endpoint(tcp::v4(), 0));
      acceptor_.listen();
      
      auto port = acceptor_.local_endpoint().port();
      
      // Create a connection
      tcp::socket server_socket(io_context_);
      acceptor_.async_accept(server_socket,
        [](std::error_code) {});
      
      socket_.open(tcp::v4());
      socket_.connect(tcp::endpoint(tcp::v4(), port));
      
      io_context_.poll();
      
      #ifdef _WIN32
      std::cout << "   - Stream sockets use zero-byte WSARecv for readiness\n";
      std::cout << "   - No buffer allocation needed for polling\n";
      std::cout << "   - Completed when data arrives\n";
      
      // Demonstrate zero-byte receive
      char dummy;
      socket_.async_receive(asio::buffer(&dummy, 0),
        [](std::error_code ec, std::size_t bytes)
        {
          std::cout << "   - Zero-byte receive completed: " 
                    << ec.message() << "\n";
        });
      #endif
      
      socket_.close();
      server_socket.close();
      acceptor_.close();
    }
    catch (std::exception& e)
    {
      std::cout << "   - Error: " << e.what() << "\n";
    }
    
    std::cout << "\n";
  }

  void demonstrate_immediate_completion()
  {
    std::cout << "5. Immediate Completion Optimization\n";
    
    #ifdef _WIN32
    std::cout << "   - Some operations complete immediately\n";
    std::cout << "   - ASIO detects ERROR_SUCCESS (not ERROR_IO_PENDING)\n";
    std::cout << "   - Handler invoked without going through IOCP queue\n";
    std::cout << "   - Reduces latency for fast operations\n";
    #endif
    
    std::cout << "\n";
  }

  void demonstrate_cancellation()
  {
    std::cout << "6. Windows Cancellation Methods\n";
    
    #ifdef _WIN32
    std::cout << "   - Vista+: CancelIoEx (cancel from any thread)\n";
    std::cout << "   - XP: CancelIo (only from initiating thread)\n";
    std::cout << "   - ASIO tracks safe_cancellation_thread_id\n";
    std::cout << "   - Socket closure as ultimate cancellation\n";
    
    // Check for CancelIoEx availability
    HMODULE kernel32 = ::GetModuleHandleA("kernel32.dll");
    if (kernel32)
    {
      auto cancel_io_ex = ::GetProcAddress(kernel32, "CancelIoEx");
      if (cancel_io_ex)
      {
        std::cout << "   - CancelIoEx is available on this system\n";
      }
      else
      {
        std::cout << "   - CancelIoEx not available (pre-Vista)\n";
      }
    }
    #endif
    
    std::cout << "\n";
  }

private:
  asio::io_context& io_context_;
  tcp::socket socket_;
  tcp::acceptor acceptor_;
};

// Demonstrate custom IOCP completion key
class custom_iocp_service
{
public:
  custom_iocp_service(asio::io_context& io_context)
    : io_context_(io_context)
  {
    #ifdef _WIN32
    // Get the IOCP handle from io_context
    // Note: This is implementation-specific and not portable
    demonstrate_iocp_internals();
    #endif
  }

private:
  void demonstrate_iocp_internals()
  {
    std::cout << "=== IOCP Internals ===\n";
    std::cout << "- io_context creates an IOCP handle\n";
    std::cout << "- All async operations use OVERLAPPED structures\n";
    std::cout << "- Completion key identifies the io_context\n";
    std::cout << "- OVERLAPPED pointer identifies the operation\n";
    std::cout << "\n";
  }

  asio::io_context& io_context_;
};

int main()
{
  try
  {
    asio::io_context io_context;
    
    // Demonstrate Windows-specific features
    windows_socket_demo demo(io_context);
    demo.demonstrate_features();
    
    // Show IOCP internals
    custom_iocp_service iocp_demo(io_context);
    
    // Performance tips
    std::cout << "=== Windows Performance Tips ===\n";
    std::cout << "1. Use thread pool size = CPU core count\n";
    std::cout << "2. Enable TCP_NODELAY for low latency\n";
    std::cout << "3. Increase socket buffers for throughput\n";
    std::cout << "4. Use SO_REUSEADDR for server sockets\n";
    std::cout << "5. Consider SO_CONDITIONAL_ACCEPT for servers\n";
    std::cout << "6. Pre-allocate buffers to avoid allocation overhead\n";
    std::cout << "\n";
    
    // Windows vs POSIX comparison
    std::cout << "=== Windows vs POSIX ===\n";
    std::cout << "Windows advantages:\n";
    std::cout << "- True async I/O (kernel completes operations)\n";
    std::cout << "- Better scalability for high connection counts\n";
    std::cout << "- Natural thread pool integration\n";
    std::cout << "- No need to poll for readiness\n";
    std::cout << "\n";
    std::cout << "POSIX advantages:\n";
    std::cout << "- Lower latency for ready operations\n";
    std::cout << "- Simpler programming model\n";
    std::cout << "- Better for low connection counts\n";
    std::cout << "- More predictable behavior\n";
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

/*
Compilation:
- Windows: cl /EHsc windows_specific_features.cpp /I<asio_include> /link ws2_32.lib mswsock.lib
- Linux/Mac: This code will compile but Windows-specific features won't be demonstrated

Key Takeaways:
1. IOCP provides true async I/O without blocking threads
2. Operations complete in kernel space
3. Excellent scalability for thousands of connections
4. Different error codes and cancellation model than POSIX
5. Some operations (ConnectEx, AcceptEx) are Windows-only
*/