#ifndef ELEMENT_HH_
#define ELEMENT_HH_

#include <functional>
#include <memory>

#include "Packet.hh"

/** @file Element.hh
 * This modules contains a Click-inspired, modern C++ implementation of
 * push-pull elements.
 */

struct In;
struct Out;

/** @brief Type tag for input ports */
struct In { using Flip = Out; };

/** @brief Type tag for output ports */
struct Out { using Flip = In; };

/** @brief Type tag for push ports */
struct Push {};

/** @brief Type tag for pull ports */
struct Pull {};

/** @brief A packet processing element */
class Element {
public:
    Element() = default;
    virtual ~Element() = default;
};

template <class D, class P, class T>
class Port;

/** @brief Base class representing a port attached to an Element. */
template <class D, class P, class T>
class BasePort {
public:
    friend class Port<typename D::Flip, P, T>;
    friend class BasePort<typename D::Flip, P, T>;

    BasePort(Element& element,
             std::function<void(void)> connected,
             std::function<void(void)> disconnected)
      : element_(element)
      , connected_(connected)
      , disconnected_(disconnected)
    {
    }

    virtual ~BasePort() = default;

    /** @brief Return this Port%'s Element. */
    Element& element(void)
    {
        return element_;
    }

    /** @brief Return this Port%'s partner Port. */
    BasePort<typename D::Flip, P, T> *partner(void)
    {
        return partner_;
    }

    /** @brief Return true if the Port is connected. */
    bool isConnected(void)
    {
        return partner_ != nullptr;
    }

protected:
    /** @brief The Element to which this Port is attached. */
    Element& element_;

    /** @brief This Port%'s partner Port. */
    BasePort<typename D::Flip, P, T> *partner_;

    /** @brief Called when the port is connected. */
    std::function<void(void)> connected_;

    /** @brief Called when the port is disconnected. */
    std::function<void(void)> disconnected_;

    /** @brief Safely indicate that the port was connected. */
    void connected(void)
    {
        if (connected_)
            connected_();
    }

    /** @brief Safely indicate that the port was disconnected. */
    void disconnected(void)
    {
        if (disconnected_)
            disconnected_();
    }
};

/** @brief A port attached to an Element. */
template <class D, class P, class T>
class Port : public BasePort<D,P,T> {
public:
    Port() = delete;
    virtual ~Port() = default;
};

/** @brief A push input port */
template <class T>
class Port<In, Push, T> : public BasePort<In, Push, T> {
public:
    friend class Port<Out, Push, T>;

    Port() = delete;
    Port(const Port&) = delete;
    Port(Port&&) = delete;
    Port& operator =(const Port&) = delete;
    Port& operator =(Port&&) = delete;

    Port(Element& element,
         std::function<void(void)> connected,
         std::function<void(void)> disconnected,
         std::function<void(T&&)> send)
      : BasePort<In, Push, T>(element, connected, disconnected)
      , send_(send)
    {
    }

    virtual ~Port() = default;

    /** @brief Send a packet to the port. */
    void send(const T& pkt)
    {
        send_(std::move(T(pkt)));
    }

    /** @brief Send a packet to the port. */
    void send(T&& pkt)
    {
        send_(std::move(pkt));
    }

protected:
    /** @brief Called to send a packet. */
    std::function<void(T&&)> send_;
};

/** @brief A pull input port */
template <class T>
class Port<In, Pull, T> : public BasePort<In, Pull, T> {
public:
    Port() = delete;
    Port(const Port&) = delete;
    Port(Port&&) = delete;
    Port& operator =(const Port&) = delete;
    Port& operator =(Port&&) = delete;

    Port(Element& element,
         std::function<void(void)> connected,
         std::function<void(void)> disconnected)
      : BasePort<In, Pull, T>(element, connected, disconnected)
      , upstream_(nullptr)
    {
    }

    virtual ~Port()
    {
        disconnect();
    }

    /** @brief Pull a packet from the port. */
    bool pull(T& pkt)
    {
        if (upstream_)
            return upstream_->recv(pkt);
        else
            return false;
    }

    /** @brief Connect the port to an upstream pull port. */
    void connect(std::shared_ptr<Element> element, Port<Out, Pull, T>* p)
    {
        if (upstream_ != nullptr)
            throw std::exception();

        upstream_element_ = element;
        upstream_ = p;
        this->partner_ = p;
        this->connected();
        this->partner_->connected();
    }

    /** @brief Disconnect the port from its upstream pull port. */
    void disconnect(void)
    {
        if (upstream_ == nullptr)
            return;

        this->partner_->disconnected();
        this->disconnected();
        this->partner_ = nullptr;
        upstream_ = nullptr;
        upstream_element_.reset();
    }

protected:
    /** @brief The upstream pull port. */
    Port<Out, Pull, T>* upstream_;

    /** @brief The upstream pull port's Element. */
    std::shared_ptr<Element> upstream_element_;
};

/** @brief A push output port */
template <class T>
class Port<Out, Push, T> : public BasePort<Out, Push, T> {
public:
    Port() = delete;
    Port(const Port&) = delete;
    Port(Port&&) = delete;
    Port& operator =(const Port&) = delete;
    Port& operator =(Port&&) = delete;

    Port(Element& element,
         std::function<void(void)> connected,
         std::function<void(void)> disconnected)
      : BasePort<Out, Push, T>(element, connected, disconnected)
      , downstream_(nullptr)
    {
    }

    virtual ~Port()
    {
        disconnect();
    }

    /** @brief Push a packet out the port. */
    void push(const T& pkt)
    {
        if (downstream_)
            downstream_->send(pkt);
    }

    /** @brief Push a packet out the port. */
    void push(T&& pkt)
    {
        if (downstream_)
            downstream_->send(std::move(pkt));
    }

    /** @brief Connect the port to a downstream push port. */
    void connect(std::shared_ptr<Element> element, Port<In, Push, T>* p)
    {
        if (downstream_ != nullptr)
            throw std::exception();

        downstream_element_ = element;
        downstream_ = p;
        this->partner_ = p;
        this->connected();
        this->partner_->connected();
    }

    /** @brief Disconnect the port from its downstream push port. */
    void disconnect(void)
    {
        if (downstream_ == nullptr)
            return;

        this->partner_->disconnected();
        this->disconnected();
        this->partner_ = nullptr;
        downstream_ = nullptr;
        downstream_element_.reset();
    }

protected:
    /** @brief The downstream push port. */
    Port<In, Push, T>* downstream_;

    /** @brief The downstream push port's Element. */
    std::shared_ptr<Element> downstream_element_;
};

/** @brief A pull output port */
template <class T>
class Port<Out, Pull, T> : public BasePort<Out, Pull, T> {
public:
    friend class Port<In, Pull, T>;

    Port() = delete;
    Port(const Port&) = delete;
    Port(Port&&) = delete;
    Port& operator =(const Port&) = delete;
    Port& operator =(Port&&) = delete;

    Port(Element& element,
         std::function<void(void)> connected,
         std::function<void(void)> disconnected,
         std::function<bool(T&)> recv)
      : BasePort<Out, Pull, T>(element, connected, disconnected)
      , recv_(recv)
    {
    }

    virtual ~Port() = default;

    /** @brief Receive a packet from the port. */
    bool recv(T& pkt)
    {
        return recv_(pkt);
    }

protected:
    /** @brief Called to receive a packet. */
    std::function<bool(T&)> recv_;
};

template <typename D>
using NetIn = Port<In,D,std::shared_ptr<NetPacket>>;

template <typename D>
using NetOut = Port<Out,D,std::shared_ptr<NetPacket>>;

template <typename D>
using RadioIn = Port<In,D,std::shared_ptr<RadioPacket>>;

template <typename D>
using RadioOut = Port<Out,D,std::shared_ptr<RadioPacket>>;

#endif /* ELEMENT_HH_ */
