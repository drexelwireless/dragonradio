#include <functional>

#include "Controller.hh"

using namespace std::placeholders;

Controller::Controller(std::shared_ptr<Net> net)
  : net_in(*this, nullptr, nullptr)
  , net_out(*this, nullptr, std::bind(&Controller::disconnect, this), std::bind(&Controller::pull, this, _1))
  , radio_in(*this, nullptr, nullptr, std::bind(&Controller::received, this, _1))
  , radio_out(*this, nullptr, nullptr)
  , net_(net)
{
}

void Controller::disconnect(void)
{
    net_in.disconnect();
}
