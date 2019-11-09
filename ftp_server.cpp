﻿/// very simple ftp server by yasio, only support file retrive command

#include "ftp_server.hpp"

ftp_server::ftp_server(cxx17::string_view root)
{
  ftp_session::register_handlers_once();
  root_ = root;
}
void ftp_server::run(u_short port)
{
  // max support 10 clients
  std::vector<io_hostent> hosts{{"0.0.0.0", 21}};

  for (auto i = 1; i <= 10; ++i)
  {
    hosts.push_back({"0.0.0.0", 0});
    this->avails_.push_back(i);
  }

  service_.set_option(YOPT_NO_NEW_THREAD, 1);
  service_.set_option(YOPT_DEFER_EVENT, 0);
  service_.set_option(YOPT_DEFER_HANDLER, 0);

  service_.schedule(std::chrono::seconds(1), [=](bool) {
    service_.set_option(YOPT_CHANNEL_LFBFD_PARAMS, 0, 16384, -1, 0, 0);
    service_.set_option(YOPT_CHANNEL_LFBFD_PARAMS, 1, 16384, -1, 0, 0);
    service_.open(0, YCM_TCP_SERVER);
  });

  service_.start_service(&hosts.front(), hosts.size(), [=](event_ptr&& ev) {
    auto thandle = ev->transport();
    switch (ev->kind())
    {
      case YEK_PACKET:
        if (ev->cindex() == 0)
        {
          dispatch_packet(thandle, std::move(ev->packet()));
        }
        else
        {
          ; // ignore
        }
        break;
      case YEK_CONNECT_RESPONSE:
        if (ev->status() == 0)
        {
          if (ev->cindex() == 0)
          {
            on_open_session(thandle);
          }
          else
          {
            on_open_transmit_session(ev->cindex(), thandle);
          }
        }
        break;
      case YEK_CONNECTION_LOST:
        if (ev->cindex() == 0)
        {
          on_close_session(thandle);
        }
        break;
    }
  });
}

void ftp_server::on_open_session(transport_handle_t thandle)
{
  if (!this->avails_.empty())
  {
    auto cindex  = this->avails_.back();
    thandle->ud_ = reinterpret_cast<void*>(cindex);
    auto result = this->sessions_.emplace(cindex, std::make_shared<ftp_session>(*this, thandle));
    if (result.second)
    {
      this->avails_.pop_back();
      result.first->second->say_hello();
    }
    else
    {
      assert(false);
      service_.close(thandle);
    }
  }
  else
  { // close directly
    service_.close(thandle);
  }
}

void ftp_server::on_close_session(transport_handle_t thandle)
{
  auto it = this->sessions_.find(reinterpret_cast<int>(thandle->ud_));
  if (it != this->sessions_.end())
  {
    printf("the ftp session:%p is ended, cindex=%d\n", thandle, it->first);
    this->avails_.push_back(it->first);
    this->sessions_.erase(it);
  }
}

void ftp_server::on_open_transmit_session(int cindex, transport_handle_t thandle)
{
  auto it = this->sessions_.find(cindex);
  if (it != this->sessions_.end())
  {
    it->second->open_transimt_session(thandle);
  }
  else
  {
    printf("Error: no ftp session for file transfer channel: %d", cindex);
    service_.close(thandle);
  }
}

void ftp_server::dispatch_packet(transport_handle_t thandle, std::vector<char>&& packet)
{
  auto it = this->sessions_.find(reinterpret_cast<int>(thandle->ud_));
  if (it != this->sessions_.end())
  {
    it->second->handle_packet(packet);
  }
  else
  {
    printf("Error: cann't dispatch for a unregistered session: %p, will close it.", thandle);
    service_.close(thandle);
  }
}

int main(int argc, char** argv)
{
  if (argc < 2)
    return EINVAL;

  std::string_view wwwroot = argv[1];
  if (!fsutils::is_dir_exists(wwwroot))
    return ENOENT;

  ftp_server server(wwwroot);

  server.run();

  return 0;
}