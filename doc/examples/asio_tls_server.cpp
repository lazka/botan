#include <iostream>
#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <asio.hpp>

#include <botan/tls_server.h>
#include <botan/x509cert.h>
#include <botan/pkcs8.h>
#include <botan/auto_rng.h>
#include <botan/init.h>

using Botan::byte;
using asio::ip::tcp;

class tls_server_session : public boost::enable_shared_from_this<tls_server_session>
   {
   public:
      typedef boost::shared_ptr<tls_server_session> pointer;

      static pointer create(asio::io_service& io_service,
                            Botan::TLS_Session_Manager& session_manager,
                            Botan::Credentials_Manager& credentials,
                            Botan::TLS_Policy& policy,
                            Botan::RandomNumberGenerator& rng)
         {
         return pointer(
            new tls_server_session(
               io_service,
               session_manager,
               credentials,
               policy,
               rng)
            );
         }

      tcp::socket& socket() { return m_socket; }

      void start()
         {
         m_socket.async_read_some(
            asio::buffer(m_read_buf, sizeof(m_read_buf)),
            boost::bind(&tls_server_session::handle_read, shared_from_this(),
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred));
         }

      void stop() { m_socket.close(); }

   private:
      tls_server_session(asio::io_service& io_service,
                         Botan::TLS_Session_Manager& session_manager,
                         Botan::Credentials_Manager& credentials,
                         Botan::TLS_Policy& policy,
                         Botan::RandomNumberGenerator& rng) :
         m_socket(io_service),
         m_tls(boost::bind(&tls_server_session::tls_output_wanted, this, _1, _2),
               boost::bind(&tls_server_session::tls_data_recv, this, _1, _2, _3),
               boost::bind(&tls_server_session::tls_handshake_complete, this, _1),
               session_manager,
               credentials,
               policy,
               rng)
         {
         }

      void handle_read(const asio::error_code& error,
                       size_t bytes_transferred)
         {
         if(!error)
            {
            try
               {
               m_tls.received_data(m_read_buf, bytes_transferred);
               }
            catch(std::exception& e)
               {
               printf("Failed - %s\n", e.what());
               stop();
               return;
               }

            m_socket.async_read_some(
               asio::buffer(m_read_buf, sizeof(m_read_buf)),
               boost::bind(&tls_server_session::handle_read, shared_from_this(),
                           asio::placeholders::error,
                           asio::placeholders::bytes_transferred));
            }
         else
            {
            stop();
            //printf("Error in read: %s\n", error.message().c_str());
            }
         }

      void handle_write(const asio::error_code& error,
                        size_t bytes_transferred)
         {
         if(!error)
            {
            m_write_buf.clear();

            // initiate another write if needed
            tls_output_wanted(NULL, 0);
            }
         else
            {
            //printf("Error in write: %s\n", error.message().c_str());
            stop();
            }
         }

      void tls_output_wanted(const byte buf[], size_t buf_len)
         {
         if(buf_len > 0)
            m_outbox.insert(m_outbox.end(), buf, buf + buf_len);

         // no write pending and have output pending
         if(m_write_buf.empty() && !m_outbox.empty())
            {
            std::swap(m_outbox, m_write_buf);

            asio::async_write(m_socket,
                              asio::buffer(&m_write_buf[0], m_write_buf.size()),
                              boost::bind(&tls_server_session::handle_write,
                                          shared_from_this(),
                                          asio::placeholders::error,
                                          asio::placeholders::bytes_transferred));
            }
         }

      void tls_data_recv(const byte buf[], size_t buf_len, Botan::u16bit alert_info)
         {
         if(buf_len == 0 && alert_info != Botan::NULL_ALERT)
            {
            //printf("Alert: %d\n", alert_info);
            if(alert_info == 0)
               {
               m_tls.close();
               return;
               }
            }

         if(buf_len > 4) // FIXME: ghetto
            {
            std::string out;
            out += "\r\n";
            out += "HTTP/1.0 200 OK\r\n";
            out += "Server: Botan ASIO test server\r\n";
            out += "Host: 192.168.10.5\r\n";
            out += "Content-Type: text/html\r\n";
            out += "\r\n";
            out += "<html><body>Greets. You said: ";
            out += std::string((const char*)buf, buf_len);
            out += "</body></html>\r\n\r\n";

            m_tls.queue_for_sending(reinterpret_cast<const byte*>(&out[0]),
                                    out.size());
            m_tls.close();
            }
         }

      bool tls_handshake_complete(const Botan::TLS_Session& session)
         {
         return true;
         }

      tcp::socket m_socket;
      Botan::TLS_Server m_tls;

      unsigned char m_read_buf[Botan::MAX_TLS_RECORD_SIZE];

      // used to hold the data currently being written by the system
      std::vector<byte> m_write_buf;

      // used to hold data queued for writing
      std::vector<byte> m_outbox;
   };

class Credentials_Manager_Simple : public Botan::Credentials_Manager
   {
   public:
      Credentials_Manager_Simple(Botan::RandomNumberGenerator& rng) : rng(rng) {}

      std::vector<Botan::X509_Certificate> cert_chain(
         const std::string& cert_key_type,
         const std::string& type,
         const std::string& context)
         {
         const std::string hostname = (context == "" ? "localhost" : context);

         Botan::X509_Certificate cert(hostname + ".crt");
         Botan::Private_Key* key = Botan::PKCS8::load_key(hostname + ".key", rng);

         certs_and_keys[cert] = key;

         std::vector<Botan::X509_Certificate> certs;
         certs.push_back(cert);
         return certs;
         }

      Botan::Private_Key* private_key_for(const Botan::X509_Certificate& cert,
                                          const std::string& type,
                                          const std::string& context)
         {
         return certs_and_keys[cert];
         }

   private:
      Botan::RandomNumberGenerator& rng;
      std::map<Botan::X509_Certificate, Botan::Private_Key*> certs_and_keys;
   };

class Server_TLS_Policy : public Botan::TLS_Policy
   {
   public:
      //bool require_client_auth() const { return true; }

      bool check_cert(const std::vector<Botan::X509_Certificate>& certs) const
         {
         for(size_t i = 0; i != certs.size(); ++i)
            {
            std::cout << certs[i].to_string();
            }

         std::cout << "Warning: not checking cert signatures\n";

         return true;
         }
   };

class tls_server
   {
   public:
      typedef tls_server_session session;

      tls_server(asio::io_service& io_service, unsigned short port) :
         m_acceptor(io_service, tcp::endpoint(tcp::v4(), port)),
         m_creds(m_rng)
         {
         session::pointer new_session = make_session();

         m_acceptor.async_accept(
            new_session->socket(),
            boost::bind(
               &tls_server::handle_accept,
               this,
               new_session,
               asio::placeholders::error)
            );
         }

   private:
      session::pointer make_session()
         {
         return session::create(
            m_acceptor.io_service(),
            m_session_manager,
            m_creds,
            m_policy,
            m_rng
            );
         }

      void handle_accept(session::pointer new_session,
                         const asio::error_code& error)
         {
         if (!error)
            {
            new_session->start();

            new_session = make_session();

            m_acceptor.async_accept(
               new_session->socket(),
               boost::bind(
                  &tls_server::handle_accept,
                  this,
                  new_session,
                  asio::placeholders::error)
               );
            }
         }

      tcp::acceptor m_acceptor;

      Botan::AutoSeeded_RNG m_rng;
      Botan::TLS_Session_Manager_In_Memory m_session_manager;
      Server_TLS_Policy m_policy;
      Credentials_Manager_Simple m_creds;
   };

int main()
   {
   try
      {
      Botan::LibraryInitializer init;
      asio::io_service io_service;

      unsigned short port = 4433;
      tls_server server(io_service, port);
      io_service.run();
      }
   catch (std::exception& e)
      {
      std::cerr << e.what() << std::endl;
      }

   return 0;
   }

