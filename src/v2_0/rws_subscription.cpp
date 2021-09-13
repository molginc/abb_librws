#include <abb_librws/v1_0/rws_subscription.h>
#include <abb_librws/rws_error.h>
#include <abb_librws/parsing.h>

#include <Poco/Net/HTTPRequest.h>
#include <Poco/DOM/NodeList.h>

#include <boost/exception/diagnostic_information.hpp>

#include <iostream>


namespace abb :: rws :: v2_0
{
  using namespace Poco::Net;


  SubscriptionGroup::SubscriptionGroup(RWSClient& client, SubscriptionResources const& resources, SubscriptionCallback& callback)
  : client_ {client}
  {
    // Generate content for a subscription HTTP post request.
    std::stringstream subscription_content;
    for (std::size_t i = 0; i < resources.size(); ++i)
    {
      subscription_content << "resources=" << i
                            << "&"
                            << i << "=" << resources[i].getURI(*this)
                            << "&"
                            << i << "-p=" << static_cast<int>(resources[i].getPriority())
                            << (i < resources.size() - 1 ? "&" : "");
    }

    std::string content_type = "application/x-www-form-urlencoded;v=2.0";

    // Make a subscription request.
    POCOResult const poco_result = http_client_.httpPost(Services::SUBSCRIPTION, subscription_content.str(), content_type);

    if (poco_result.httpStatus() != HTTPResponse::HTTP_CREATED)
      BOOST_THROW_EXCEPTION(
        ProtocolError {"Unable to create Subscription"}
        << HttpStatusErrorInfo {poco_result.httpStatus()}
        << HttpReasonErrorInfo {poco_result.reason()}
        << HttpMethodErrorInfo {HTTPRequest::HTTP_POST}
        << HttpRequestContentErrorInfo {subscription_content.str()}
        << HttpResponseContentErrorInfo {poco_result.content()}
        << HttpResponseErrorInfo {poco_result}
        << UriErrorInfo {Services::SUBSCRIPTION}
      );

    // Find "Location" header attribute
    auto const h = std::find_if(
      poco_result.headerInfo().begin(), poco_result.headerInfo().end(),
      [] (auto const& p) { return p.first == "Location"; });

    if (h != poco_result.headerInfo().end())
    {
      std::string const poll = "/poll/";
      auto const start_postion = h->second.find(poll);

      if (start_postion != std::string::npos)
        subscription_group_id_ = h->second.substr(start_postion + poll.size());
    }

    if (subscription_group_id_.empty())
      BOOST_THROW_EXCEPTION(ProtocolError {"Cannot get subscription group from HTTP response"});
  }


  SubscriptionGroup::SubscriptionGroup(SubscriptionGroup&& rhs)
  : client_ {rhs.client_}
  , subscription_group_id_ {rhs.subscription_group_id_}
  {
    // Clear subscription_group_id_ of the SubscriptionGroup that has been moved from,
    // s.t. its destructor does not close the subscription.
    rhs.subscription_group_id_.clear();
  }


  SubscriptionGroup::~SubscriptionGroup()
  {
    close();
  }


  void SubscriptionGroup::close()
  {
    if (!subscription_group_id_.empty())
    {
      // Unsubscribe from events
      httpDelete(Services::SUBSCRIPTION + "/" + subscription_group_id);
    }
  }


  void SubscriptionGroup::detach() noexcept
  {
    subscription_group_id_.clear();
  }


  SubscriptionReceiver SubscriptionGroup::receive() const
  {
    return SubscriptionReceiver {client_, subscription_group_id_};
  }


  std::string SubscriptionGroup::getResourceURI(IOSignalResource const& io_signal) const
  {
    std::string resource_uri = Resources::RW_IOSYSTEM_SIGNALS;
    resource_uri += "/";
    resource_uri += io_signal.name;
    resource_uri += ";";
    resource_uri += Identifiers::STATE;
    return resource_uri;
  }


  std::string SubscriptionGroup::getResourceURI(RAPIDResource const& resource) const
  {
    std::string resource_uri = Resources::RW_RAPID_SYMBOL_DATA_RAPID;
    resource_uri += "/";
    resource_uri += resource.task;
    resource_uri += "/";
    resource_uri += resource.module;
    resource_uri += "/";
    resource_uri += resource.name;
    resource_uri += ";";
    resource_uri += Identifiers::VALUE;
    return resource_uri;
  }


  std::string SubscriptionGroup::getResourceURI(RAPIDExecutionStateResource const&) const
  {
    return "/rw/rapid/execution;ctrlexecstate";
  }


  const std::chrono::microseconds SubscriptionReceiver::DEFAULT_SUBSCRIPTION_TIMEOUT {40000000000};


  SubscriptionReceiver::SubscriptionReceiver(RWSClient& client, std::string const& subscription_group_id)
  : client_ {client}
  , webSocket_ {client_.webSocketConnect("/poll/" + subscription_group_id, "rws_subscription")}
  {
  }


  SubscriptionReceiver::~SubscriptionReceiver()
  {
  }


  bool SubscriptionReceiver::waitForEvent(SubscriptionCallback& callback, std::chrono::microseconds timeout)
  {
    WebSocketFrame frame;
    if (webSocketReceiveFrame(frame, timeout))
    {
      Poco::AutoPtr<Poco::XML::Document> doc = parser_.parseString(frame.frame_content);
      processEvent(doc, callback);
      return true;
    }

    return false;
  }


  bool SubscriptionReceiver::webSocketReceiveFrame(WebSocketFrame& frame, std::chrono::microseconds timeout)
  {
    auto now = std::chrono::steady_clock::now();
    auto deadline = std::chrono::steady_clock::now() + timeout;

    // If the connection is still active...
    int flags = 0;
    std::string content;
    int number_of_bytes_received = 0;

    // Wait for (non-ping) WebSocket frames.
    do
    {
      now = std::chrono::steady_clock::now();
      if (now >= deadline)
        BOOST_THROW_EXCEPTION(TimeoutError {"WebSocket frame receive timeout"});

      webSocket_.setReceiveTimeout(std::chrono::duration_cast<std::chrono::microseconds>(deadline - now).count());
      flags = 0;

      try
      {
        number_of_bytes_received = webSocket_.receiveFrame(websocket_buffer_, sizeof(websocket_buffer_), flags);
      }
      catch (Poco::TimeoutException const&)
      {
        BOOST_THROW_EXCEPTION(
          TimeoutError {"WebSocket frame receive timeout"}
            << boost::errinfo_nested_exception(boost::current_exception())
        );
      }

      content = std::string(websocket_buffer_, number_of_bytes_received);

      // Check for ping frame.
      if ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_PING)
      {
        // Reply with a pong frame.
        webSocket_.sendFrame(websocket_buffer_,
                                number_of_bytes_received,
                                WebSocket::FRAME_FLAG_FIN | WebSocket::FRAME_OP_PONG);
      }
    } while ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_PING);

    // Check for closing frame.
    if ((flags & WebSocket::FRAME_OP_BITMASK) == WebSocket::FRAME_OP_CLOSE)
    {
      // Do not pass content of a closing frame to end user,
      // according to "The WebSocket Protocol" RFC6455.
      frame.frame_content.clear();
      frame.flags = flags;

      return false;
    }

    frame.flags = flags;
    frame.frame_content = content;

    return number_of_bytes_received != 0;
  }


  void SubscriptionReceiver::shutdown()
  {
    // Shut down the socket. This should make webSocketReceiveFrame() return as soon as possible.
    webSocket_.shutdown();
  }


  void SubscriptionReceiver::processEvent(Poco::AutoPtr<Poco::XML::Document> doc, SubscriptionCallback& callback) const
  {
    // IMPORTANT: don't use AutoPtr<XML::Node> here! Otherwise you will get memory corruption.
    Poco::XML::Node const * li_node = doc->getNodeByPath("html/body/div/ul/li");
    if (!li_node)
      BOOST_THROW_EXCEPTION(ProtocolError {"Cannot parse RWS event message: can't find XML path html/body/div/ul/li"});

    auto const * a_node = li_node->getNodeByPath("a");
    if (!a_node)
      BOOST_THROW_EXCEPTION(ProtocolError {"Cannot parse RWS event message: can't find XML path html/body/div/ul/li/a"});

    std::string uri;
    uri = xmlNodeGetAttributeValue(a_node, "href");

    std::string const class_attribute_value = xmlNodeGetAttributeValue(li_node, "class");

    if (class_attribute_value == "ios-signalstate-ev")
    {
      IOSignalStateEvent event;
      std::string const prefix = "/rw/iosystem/signals/";

      if (uri.find(prefix) != 0)
        BOOST_THROW_EXCEPTION(ProtocolError {"Cannot parse RWS event message: invalid resource URI"} << UriErrorInfo {uri});

      event.signal = uri.substr(prefix.length(), uri.find(";") - prefix.length());
      event.value = xmlFindTextContent(li_node, XMLAttribute {"class", "lvalue"});
      callback.processEvent(event);
    }
    else if (class_attribute_value == "rap-ctrlexecstate-ev")
    {
      RAPIDExecutionStateEvent event;

      std::string const state_string = xmlFindTextContent(li_node, XMLAttribute {"class", "ctrlexecstate"});
      if (state_string == "running")
        event.state = RAPIDExecutionState::running;
      else if (state_string == "stopped")
        event.state = RAPIDExecutionState::stopped;
      else
        BOOST_THROW_EXCEPTION(ProtocolError {"Cannot parse RWS event message: invalid RAPID execution state string"});

      callback.processEvent(event);
    }
    else
      BOOST_THROW_EXCEPTION(ProtocolError {"Cannot parse RWS event message: unrecognized class " + class_attribute_value});
  }


  void SubscriptionCallback::processEvent(IOSignalStateEvent const& event)
  {
  }


  void SubscriptionCallback::processEvent(RAPIDExecutionStateEvent const& event)
  {
  }

}