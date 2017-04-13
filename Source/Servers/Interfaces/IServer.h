/*
    Initial author: Convery
    Started: 2017-4-13
    License: Apache 2.0
*/

#pragma once
#include <cstdint>

// The server capabilities flags, can be combined.
#define ISERVER_BASE        0
#define ISERVER_EXTENDED    1
#define ISERVER_RESERVED1   2
#define ISERVER_RESERVED2   4
#define ISERVER_RESERVED3   8

// The base server for single-socket connections.
struct IServer
{
    // Return the capabilities of this server.
    virtual const uint64_t Capabilities() { return ISERVER_BASE; };

    // Returns false if there's an error, like there being no data or connection.
    virtual bool onWriterequest(const void *Databuffer, const uint32_t Datasize) = 0;
    virtual bool onReadrequest(void *Databuffer, uint32_t *Datasize /* IN - Buffer size, OUT - Bytes read */) = 0;
};

// The base server for multi-socket connections.
struct IServerEx : public IServer
{
    // Return the capabilities of this server.
    virtual const uint64_t Capabilities() { return ISERVER_EXTENDED; };

    // Nullsub the base IO-callbacks as they aren't implemented in derived callbacks.
    virtual bool onReadrequest(void *Databuffer, uint32_t *Datasize) { (void)Databuffer; (void)Datasize; return false; };
    virtual bool onWriterequest(const void *Databuffer, const uint32_t Datasize) { (void)Databuffer; (void)Datasize; return false; };

    // Returns false if there's an error, like there being no data or connection.
    virtual bool onWriterequestEx(const size_t Socket, const void *Databuffer, const uint32_t Datasize) = 0;
    virtual bool onReadrequestEx(const size_t Socket, void *Databuffer, uint32_t *Datasize /* IN - Buffer size, OUT - Bytes read */) = 0;

    // Socket state callbacks.
    virtual void onDisconnect(const size_t Socket) = 0;
    virtual void onConnect(const size_t Socket, const uint16_t Port) = 0;
};
