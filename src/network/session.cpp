/*  Copyright (C) 2011 Twl
	(http://www.github.com/twl)

	Project: http://www.github.com/twl/kyra
*/

#include "../StdAfx.h"

namespace fbi
{
	namespace network
	{
		Session::Session(boost::asio::io_service& io_service) : socket_(io_service), initialized_(false), authorized_(false),
			register_timeout_(io_service), connection_timeout_(io_service), closing_connection_(false), ping_sent_(false)
		{
			Log.Notice("Session", "Session indul...");
			Log.Notice("Session", "Összes handler regisztrálása.");
			registration_handlers_["CONNECT"] = &Session::MessageConnect;
			message_handlers_["ASDD"] = &Session::MessageIgnore;
			/*message_handlers_["QUIT"] = &Session::MessageQuit;
			message_handlers_["PING"] = &Session::MessagePing;
			message_handlers_["PONG"] = &Session::MessagePong;*/
		}

		void Session::Start()
		{
			initialized_ = true;
			register_timeout_.expires_from_now(boost::posix_time::seconds(5));
			register_timeout_.async_wait(boost::bind(&Session::HandleRegisterTimeout, shared_from_this(), boost::asio::placeholders::error));
			boost::asio::async_read_until(socket_, buffer_, '\n', boost::bind(&Session::HandleRead, shared_from_this(),
				boost::asio::placeholders::error));
		}

		void Session::Deliver(const string& msg)
		{
			if(msg.empty())
				return;

			ChatMessage info(new string(msg));
			Deliver(info);
		}

		void Session::Deliver(const ChatMessage& msg)
		{
			if(!msg || msg->empty())
				return;

			boost::unique_lock<boost::mutex> lock(sync_);
			bool write_in_progress = !write_msgs_.empty();
			write_msgs_.push_back(msg);

			if(!write_in_progress)
				WriteNextMessage();
		}

		void Session::Authorize()
		{
			//if(bridge_.Authorize(nick_, password_))
			//{
				//cerr<<"AUTH "<<nick_<<" success\n";
				authorized_ = true;
				register_timeout_.cancel();
				connection_timeout_.expires_from_now(boost::posix_time::seconds(PingInterval));
				connection_timeout_.async_wait(boost::bind(&Session::HandleConnectionTimeout, shared_from_this(),
					boost::asio::placeholders::error));
				stringstream strstr;
				//WriteServerHeader(strstr, "001")<<":Hi "<<nick_<<"\n";
				//WriteServerHeader(strstr, "002")<<":Your host is "<<bridge_.GetServerName()<<", running version 0.0.0\n";
				//WriteServerHeader(strstr, "003")<<":This server was created 0\n";
				//WriteServerHeader(strstr, "004")<<":"<<bridge_.GetServerName()<<" 0.0.0 - n\n";
				//WriteServerHeader(strstr, "375")<<":- "<<bridge_.GetServerName()<<" "<<bridge_.GetMOTDStart()<<" -\n";
				//WriteServerHeader(strstr, "372")<<":- "<<bridge_.GetMOTD()<<"\n";
				//const string&  auto_join = bridge_.GetAutoJoin();
				//if(!auto_join.empty())
				//{
				//	if(bridge_.JoinChannel(auto_join, shared_from_this()))
				//	{
				//		active_channels_.insert(auto_join);
				//		WriteUserHeaderNoNick(strstr, "JOIN")<<auto_join<<" :"<<auto_join<<"\n";
				//	}
				//}
				strstr << "asd" << " " << "meg asd" << "\n";
				Deliver(strstr.str());
			//}
			//else
			//{
				//cerr<<"!!!: auth failed\n";
			//	Cleanup();
			//}
		}

		void Session::MessageIgnore(const string& command_id, const string& data, string& answer)
		{
		}

		void Session::MessageUnknown(const string& command_id, const string& data, string& answer)
		{
			stringstream strstr;
			//WriteServerHeader(strstr, "421")<<command_id<<" :Command "<<command_id<<" is unknown or unsupported"<<"\n";
			strstr << "asd" << " " << "meg asd" << "\n";
			answer = strstr.str();
		}

		void Session::MessageConnect(const string& command_id, const string& data, string& answer)
		{
			Authorize();
		}

		/*void Session::MessageQuit(const string& command_id, const string& data, string& answer)
		{
			//cerr<<"!!!: quit\n";
			Cleanup();
		}

		void Session::MessagePing(const string& command_id, const string& data, string& answer)
		{
			stringstream strstr;
			WriteServerHeaderNoNick(strstr, "PONG")<<bridge_.GetServerName()<<" :"<<data<<"\n";
			answer = strstr.str();
			if(!ping_sent_)
			{
				connection_timeout_.expires_from_now(boost::posix_time::seconds(PingInterval));
				connection_timeout_.async_wait(boost::bind(&Session::HandleConnectionTimeout, shared_from_this(),
					boost::asio::placeholders::error));
			}
		}

		void Session::MessagePong(const string& command_id, const string& data, string& answer)
		{
			stringstream strstr;
			if(ping_sent_)
			{
				ping_sent_ = false;
				connection_timeout_.expires_from_now(boost::posix_time::seconds(PingInterval));
				connection_timeout_.async_wait(boost::bind(&Session::HandleConnectionTimeout, shared_from_this(),
					boost::asio::placeholders::error));
			}
			answer = strstr.str();
		}*/

		void Session::HandleCommand(const string& command_data)
		{
			if(command_data.empty())
				return;

			size_t pos = command_data.find(' ');
			string command = command_data.substr(0, pos);
			string data = pos == string::npos ? "" : command_data.substr(pos + 1);

			if(!authorized_)
			{
				//cout<<command<<" "<<data<<"\n";
				map<string, MessageHandler>::iterator it = registration_handlers_.find(command);
				string answer;

				if(it != registration_handlers_.end())
					(this->*it->second)(command, data, answer);
				else
					MessageUnknown(command, data, answer);

				Deliver(answer);
			}
			else
			{
				//cout<<":"<<nick_<<"!"<<nick_<<" "<<command<<" "<<data<<"\n";
				map<string, MessageHandler>::iterator it = message_handlers_.find(command);
				string answer;

				if(it != message_handlers_.end())
					(this->*it->second)(command, data, answer);
				else
					MessageUnknown(command, data, answer);

				Deliver(answer);
			}
		}

		void Session::HandleRead(const boost::system::error_code& error)
		{
			if(!error)
			{
				istream is(&buffer_);
				string line;
				getline(is, line);

				if(line.length() > 0 && line[line.length()-1] == '\r')
					line.resize(line.length()-1);

				HandleCommand(line);

				if(socket_.is_open())
				{
					boost::asio::async_read_until(socket_, buffer_, '\n', boost::bind(&Session::HandleRead, shared_from_this(),
						boost::asio::placeholders::error));
				}
			}
			else
			{
				Log.Error("Session", "Bejövő adatok olvasása sikertelen!");
				Cleanup();
			}
		}

		void Session::HandleWrite(const boost::system::error_code& error)
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
				Cleanup();
			}
		}

		void Session::HandleRegisterTimeout(const boost::system::error_code& error)
		{
			if(!error)
			{
				closing_connection_ = true;
				Deliver("ERROR: registration timeout\n");
			}
		}

		void Session::HandleConnectionTimeout(const boost::system::error_code& error)
		{
			if(!error)
			{
				if(ping_sent_)
				{
					closing_connection_ = true;
					Deliver("ERROR: connection timeout\n");
				}
				else
				{
					ping_sent_ = true;
					connection_timeout_.expires_from_now(boost::posix_time::seconds(30));
					connection_timeout_.async_wait(boost::bind(&Session::HandleConnectionTimeout, shared_from_this(),
						boost::asio::placeholders::error));
					stringstream strstr;
					strstr<<"PING :"/*<<bridge_.GetServerName()*/<<"\n";
					Deliver(strstr.str());
				}
			}
		}

		void Session::WriteNextMessage()
		{
			if(!write_msgs_.empty())
			{
				//cout<<(*write_msgs_.front());
				boost::asio::async_write(socket_, boost::asio::buffer(write_msgs_.front()->c_str(), write_msgs_.front()->length()),
					boost::bind(&Session::HandleWrite, shared_from_this(), boost::asio::placeholders::error));
			}
			else
			{
				if(closing_connection_)
				{
					Log.Warning("Session", "Kapcsolat bontásra került!");
					Cleanup();
				}
			}
		}

		void Session::Cleanup()
		{
			if(initialized_)
			{
				if(socket_.is_open())
					socket_.close();

				initialized_ = false;
			}
		}
	}
}
