/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * e133-receiver.cpp
 * Copyright (C) 2011 Simon Newton
 *
 * This creates a E1.33 receiver with one (emulated) RDM responder. The node is
 * registered in slp and the RDM responder responds to E1.33 commands.
 */

#include <signal.h>
#include <sysexits.h>

#include <ola/acn/ACNPort.h>
#include <ola/base/Flags.h>
#include <ola/base/Init.h>
#include <ola/BaseTypes.h>
#include <ola/Logging.h>
#include <ola/network/InterfacePicker.h>
#include <ola/rdm/RDMControllerAdaptor.h>
#include <ola/rdm/UIDAllocator.h>
#include <ola/rdm/UID.h>
#include <ola/stl/STLUtils.h>

#include <memory>
#include <string>
#include <vector>

#include "tools/e133/SimpleE133Node.h"

using ola::network::IPV4Address;
using ola::rdm::UID;
using std::auto_ptr;
using std::string;
using std::vector;

DEFINE_string(listen_ip, "", "The IP address to listen on.");
DEFINE_bool(dummy, true, "Include a dummy responder endpoint");
DEFINE_s_uint16(lifetime, t, 300, "The value to use for the service lifetime");
DEFINE_string(uid, "7a70:00000001", "The UID of the responder.");
DEFINE_s_uint32(universe, u, 1, "The E1.31 universe to listen on.");

SimpleE133Node *simple_node;

/*
 * Terminate cleanly on interrupt.
 */
static void InteruptSignal(int signo) {
  if (simple_node)
    simple_node->Stop();
  (void) signo;
}


/*
 * Startup a node
 */
int main(int argc, char *argv[]) {
  ola::SetHelpString(
      "[options]",
      "Run a very simple E1.33 Responder.");
  ola::ParseFlags(&argc, argv);
  ola::InitLoggingFromFlags();

  auto_ptr<UID> uid(UID::FromString(FLAGS_uid));
  if (!uid.get()) {
    OLA_WARN << "Invalid UID: " << FLAGS_uid;
    ola::DisplayUsage();
    exit(EX_USAGE);
  }

  vector<E133Endpoint*> endpoints;
  auto_ptr<ola::plugin::dummy::DummyResponder> dummy_responder;
  auto_ptr<ola::rdm::DiscoverableRDMControllerAdaptor>
    discoverable_dummy_responder;

  ola::rdm::UIDAllocator uid_allocator(*uid);
  // The first uid is used for the management endpoint so we burn a UID here.
  {
    auto_ptr<UID> dummy_uid(uid_allocator.AllocateNext());
  }

  if (FLAGS_dummy) {
    auto_ptr<UID> dummy_uid(uid_allocator.AllocateNext());
    if (!dummy_uid.get()) {
      OLA_WARN << "Failed to allocate a UID for the DummyResponder.";
      exit(EX_USAGE);
    }

    dummy_responder.reset(new ola::plugin::dummy::DummyResponder(*dummy_uid));
    discoverable_dummy_responder.reset(
        new ola::rdm::DiscoverableRDMControllerAdaptor(
          *dummy_uid, dummy_responder.get()));
    endpoints.push_back(new E133Endpoint(discoverable_dummy_responder.get(),
                                         E133Endpoint::EndpointProperties()));
  }

  ola::network::Interface interface;

  {
    auto_ptr<const ola::network::InterfacePicker> picker(
      ola::network::InterfacePicker::NewPicker());
    if (!picker->ChooseInterface(&interface, FLAGS_listen_ip)) {
      OLA_INFO << "Failed to find an interface";
      exit(EX_UNAVAILABLE);
    }
  }

  SimpleE133Node::Options opts(interface.ip_address, *uid, FLAGS_lifetime);
  SimpleE133Node node(opts);
  for (unsigned int i = 0; i < endpoints.size(); i++) {
    node.AddEndpoint(i + 1, endpoints[i]);
  }
  simple_node = &node;

  if (!node.Init())
    exit(EX_UNAVAILABLE);

  // signal handler
  if (!ola::InstallSignal(SIGINT, &InteruptSignal))
    return false;

  node.Run();
  for (unsigned int i = 0; i < endpoints.size(); i++) {
    node.RemoveEndpoint(i + 1);
  }
  ola::STLDeleteElements(&endpoints);
}
