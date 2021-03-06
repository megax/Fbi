/*
 * session.cpp
 */

#include "../StdAfx.h"
using namespace boost::posix_time;

namespace fbi
{
	namespace network
	{
		session::session(boost::asio::io_service& io_service) : socket_(io_service), initialized_(false), authorized_(false),
			register_timeout_(io_service), connection_timeout_(io_service), closing_connection_(false), ping_sent_(false)
		{
			Log.Notice("Session", "Session indul...");
			// konfigból jöjjön majd az ip vagy az ip-k
			enabledlist_.push_back("127.0.0.1");
			vector<string>::iterator it;
			string ips;

			for(it = enabledlist_.begin(); it != enabledlist_.end(); it++)
				ips = " " + *it;

			Log.Debug("Session", boost::format("A következő ip(k)-ről érhető csak el a program:%1%") % ips);
			cliensname = "Cliens";
			InitHandlers();
		}

		session::~session()
		{
			cleanup();
		}

		void session::start()
		{
			initialized_ = true;
			register_timeout_.expires_from_now(boost::posix_time::seconds(5));
			register_timeout_.async_wait(boost::bind(&session::HandleRegisterTimeout, shared_from_this(), boost::asio::placeholders::error));
			boost::asio::async_read_until(socket_, buffer_, '\n', boost::bind(&session::handle_read, shared_from_this(),
				boost::asio::placeholders::error));
		}

		void session::InitHandlers()
		{
			Log.Notice("Session", "Összes handler regisztrálása.");
			registration_handlers_["CONNECT"] = &session::HandleConnect;
			message_handlers_["NAME"] = &session::HandleName;
			message_handlers_["QUIT"] = &session::HandleQuit;
			message_handlers_["PING"] = &session::HandlePing;
			message_handlers_["PONG"] = &session::HandlePong;
			message_handlers_["MESSAGE"] = &session::HandleMessage;
			message_handlers_["CHANNELLIST"] = &session::HandleChannelList;
			message_handlers_["ADDCHANNEL"] = &session::HandleAddChannel;
			message_handlers_["REMOVECHANNEL"] = &session::HandleAddChannel;
			message_handlers_["TESZT"] = &session::HandleIgnore;
		}

		void session::deliver(const string& msg)
		{
			if(msg.empty())
				return;

			ChatMessage info(new string(msg));
			deliver(info);
		}

		void session::deliver(const ChatMessage& msg)
		{
			if(!msg || msg->empty())
				return;

			boost::unique_lock<boost::mutex> lock(sync_);
			bool write_in_progress = !write_msgs_.empty();
			write_msgs_.push_back(msg);

			if(!write_in_progress)
				WriteNextMessage();
		}

		void session::handle_command(const string& command_data)
		{
			if(command_data.empty())
				return;

			size_t pos = command_data.find(' ');
			string command = command_data.substr(0, pos);
			string data = pos == string::npos ? "" : command_data.substr(pos + 1);

			if(!authorized_)
			{
				string ip = boost::lexical_cast<string>(socket_.remote_endpoint().address());
				cout << ip << endl;

				if(find(enabledlist_.begin(), enabledlist_.end(), ip) == enabledlist_.end())
				{
					Log.Warning("Session", boost::format("Nem engedélyezett ip: %1%") % ip);
					cleanup();
					return;
				}

				Log.Success("Session", boost::format("Sikeresen kapcsolódott egy kliens. Ip: %1%") % ip);

				//cout << command << " " << data << endl;
				map<string, MessageHandler>::iterator it = registration_handlers_.find(command);
				string answer;

				if(it != registration_handlers_.end())
					(this->*it->second)(command, data, answer);
				else
					HandleUnknown(command, data, answer);

				deliver(answer);
			}
			else
			{
				cout << command << " " << data << endl;

				map<string, MessageHandler>::iterator it = message_handlers_.find(command);
				string answer;

				if(data.length() > 1 && data.substr(0, 1) == ":")
					data = data.erase(0, 1);

				if(it != message_handlers_.end())
					(this->*it->second)(command, data, answer);
				else
					HandleUnknown(command, data, answer);

				deliver(answer);
			}
		}

		void session::handle_read(const boost::system::error_code& error)
		{
			if(!error)
			{
				istream is(&buffer_);
				string line;
				getline(is, line);

				if(line.length() > 0 && line[line.length()-1] == '\r')
					line.resize(line.length()-1);

				handle_command(line);

				if(socket_.is_open())
				{
					boost::asio::async_read_until(socket_, buffer_, '\n', boost::bind(&session::handle_read, shared_from_this(),
						boost::asio::placeholders::error));
				}
			}
			else
			{
				Log.Error("Session", "Bejövő adatok olvasása sikertelen!");
				cleanup();
			}
		}

		void session::handle_write(const boost::system::error_code& error)
		{
			if(!error)
			{
				boost::unique_lock<boost::mutex> lock(sync_);
				write_msgs_.pop_front();
				WriteNextMessage();
			}
			else
			{
				Log.Error("Session", "Kimenő adatok küldése sikertelen!");
				cleanup();
			}
		}

		void session::HandleRegisterTimeout(const boost::system::error_code& error)
		{
			if(!error)
			{
				closing_connection_ = true;
				deliver("ERROR: registration timeout\n");
			}
		}

		void session::HandleConnectionTimeout(const boost::system::error_code& error)
		{
			if(!error)
			{
				if(ping_sent_)
				{
					closing_connection_ = true;
					deliver("ERROR: connection timeout\n");
				}
				else
				{
					ping_sent_ = true;
					connection_timeout_.expires_from_now(boost::posix_time::seconds(30));
					connection_timeout_.async_wait(boost::bind(&session::HandleConnectionTimeout, shared_from_this(),
						boost::asio::placeholders::error));

					ptime t(second_clock::local_time());
					ping(boost::str(boost::format("Szerver idő: %1%") % UnixTime()));
				}
			}
		}

		void session::WriteNextMessage()
		{
			if(!write_msgs_.empty())
			{
				//cout << (*write_msgs_.front());
				boost::asio::async_write(socket_, boost::asio::buffer(write_msgs_.front()->c_str(), write_msgs_.front()->length()),
					boost::bind(&session::handle_write, shared_from_this(), boost::asio::placeholders::error));
			}
			else
			{
				if(closing_connection_)
				{
					Log.Warning("Session", "Kapcsolat bontásra került!");
					cleanup();
				}
			}
		}

		void session::cleanup()
		{
			if(initialized_)
			{
				if(socket_.is_open())
					socket_.close();

				// megoldani hogy crash nélkül álljanak le (részben megoldva de log üzenet kéne throw helyett)
				if(IrcClientMap.size() > 0)
				{
					for(boost::unordered_map<string, ircinfo>::iterator it = IrcClientMap.begin(); it != IrcClientMap.end(); ++it)
					{
						it->second.channels.clear();
						it->second.irc->disconnect();
					}

					IrcClientMap.clear();
					//StopIrc();
					IoServiceMap.clear();
				}

				initialized_ = false;
			}
		}

		uint64 session::UnixTime()
		{
			ptime t(second_clock::local_time());
			static const boost::posix_time::ptime unixStart = boost::posix_time::from_time_t(0);
			uint64 unixTime = (t - unixStart).total_seconds();
			return unixTime;
		}

		void session::StopIrc()
		{
			for(boost::unordered_map<string, io_service>::iterator it = IoServiceMap.begin(); it != IoServiceMap.end(); ++it)
				it->second.stop();
		}
	}
}
