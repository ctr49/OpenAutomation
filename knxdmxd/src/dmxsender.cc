/*
 * dmxsender.cc
 *
 * (c) by JNK 2012
 *
 *
 */

#include "dmxsender.h"

namespace knxdmxd
{

  dmx_addr_t
  DMX::Address(const std::string &addr)
  {
    int universe, channel;
    std::string c(addr);
    sscanf((char*) c.c_str(), "%d.%d", &universe, &channel);
    return
        (channel == -1) ?
            (universe + 512) : ((((unsigned) universe) << 9) + channel);
  }

  // initalize static variables of DMX class
  ola::OlaCallbackClientWrapper DMX::m_client;
  std::map<char, knxdmxd::pUniverse> DMX::output;
  std::list<patch_table_t> DMX::patchTable;

  bool
  DMXSender::Init()
  {
    if (!m_client.Setup())
      {
        std::clog << kLogWarning << "DMXSender: OLA client setup failed "
            << std::endl;
        return false;
      }
    if (patchTable.size())
      {
        for (std::list<patch_table_t>::const_iterator iterator =
            patchTable.begin(), end = patchTable.end(); iterator != end;
            ++iterator)
          {
            patch_table_t pEntry = *iterator;

            m_client.GetClient()->Patch(pEntry.device, pEntry.port,
                ola::OUTPUT_PORT, ola::PATCH, pEntry.universe,
                ola::NewSingleCallback(this, &DMXSender::PatchComplete));

          }

      }

    return true;
  }

  void
  Universe::Crossfade()
  {
    ola::thread::MutexLocker locker(&data_mutex);
    int i;
    int max = 512;
    unsigned long currenttime = Timer::Get();

    for (i = 511; i >= 0; max = i, i--) // find last value
      if (new_[i] || old_[i])
        break;

    for (i = 0; i < max; i++)
      {
        if ((new_[i] || old_[i]) && (new_[i] != old_[i]))
          { /* avoid calculating with only 0 or finished */
            if (currenttime > fadeend_[i])
              {
                current_[i] = old_[i] = new_[i];
                std::clog << kLogDebug
                    << "DMXSender: Finished crossfading universe "
                    << (int) universe_ << " channel " << i << " to "
                    << (int) current_[i] << std::endl;
                dmx_addr_t dmx = (universe_ << 9) + i + 1; // calculate dmx-addr
                if (knxdmxd::statusmap.count(dmx))
                  {
                    std::clog << kLogDebug
                        << "DMXSender: writing status update to KNX "
                        << std::endl;
                    knxdmxd::eib_message_t message;
                    message.value = (long) new_[i];
                    message.ga = knxdmxd::statusmap.find(dmx)->second;
                    message.dpt = 5;
                    ola::thread::MutexLocker locker(&KNXSender::mutex_toKNX);
                    KNXSender::toKNX.push(message);
                    KNXSender::condition_toKNX.Signal();
                  }
              }
            else
              {
                float p = (float) (currenttime - fadestart_[i])
                    / (fadeend_[i] - fadestart_[i]);
                float q = 1.0f - p;
                current_[i] = (unsigned char) (old_[i] * q + new_[i] * p);
              }
          }
      }
  }

  int
  DMXSender::Start()
  {
    SendDMX();
    m_client.GetSelectServer()->Run();
    //sender_running_ = true;
    return 0;
  }

  void
  DMXSender::SendDMX()
  {
    for (std::map<char, pUniverse>::const_iterator i = output.begin();
        i != output.end(); ++i)
      {
        int universe = i->first;
        i->second->Crossfade();
        if (!m_client.GetClient()->SendDmx(universe, i->second->GetBuffer()))
          { // send all universes
            m_client.GetSelectServer()->Terminate();
            std::clog << kLogWarning
                << "DMXSender: OLA failed to send universe " << universe
                << std::endl;
            sender_running_ = false;
            return;
          }
      }

    m_client.GetSelectServer()->RegisterSingleTimeout(DMX_INTERVAL,
        ola::NewSingleCallback(this, &DMXSender::SendDMX));
  }

  void
  DMXSender::Terminate()
  {

    m_client.GetSelectServer()->Terminate();
    std::map<char, pUniverse>::iterator itr;

    while (!output.empty()) {
      itr = output.begin();
      std::clog << " DMXSender deleted universe" << (int) itr->first  << std::endl;
        delete (itr->second);
        output.erase(itr->first);
      }
    //sender_running_ = false;

  }

}

