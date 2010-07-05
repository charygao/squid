
#ifndef SQUID_BODY_PIPE_H
#define SQUID_BODY_PIPE_H

#include "MemBuf.h"
#include "base/AsyncCall.h"
#include "base/AsyncJob.h"

class BodyPipe;

// Interface for those who want to produce body content for others.
// BodyProducer is expected to create the BodyPipe.
// One pipe cannot have more than one producer.
class BodyProducer: virtual public AsyncJob
{
public:
    BodyProducer():AsyncJob("BodyProducer") {}
    virtual ~BodyProducer() {}

    virtual void noteMoreBodySpaceAvailable(RefCount<BodyPipe> bp) = 0;
    virtual void noteBodyConsumerAborted(RefCount<BodyPipe> bp) = 0;

protected:
    void stopProducingFor(RefCount<BodyPipe> &pipe, bool atEof);
};

// Interface for those who want to consume body content from others.
// BodyConsumer is expected to register with an existing BodyPipe
// by calling BodyPipe::setConsumer().
// One pipe cannot have more than one consumer.
class BodyConsumer: virtual public AsyncJob
{
public:
    BodyConsumer():AsyncJob("BodyConsumer") {}
    virtual ~BodyConsumer() {}

    virtual void noteMoreBodyDataAvailable(RefCount<BodyPipe> bp) = 0;
    virtual void noteBodyProductionEnded(RefCount<BodyPipe> bp) = 0;
    virtual void noteBodyProducerAborted(RefCount<BodyPipe> bp) = 0;

protected:
    void stopConsumingFrom(RefCount<BodyPipe> &pipe);
};

// Makes raw buffer checkin/checkout interface efficient and exception-safe.
// Either append or consume operations can be performed on a checkedout buffer.
class BodyPipeCheckout
{
public:
    friend class BodyPipe;

public:
    BodyPipeCheckout(BodyPipe &pipe); // checks out
    ~BodyPipeCheckout(); // aborts checkout unless checkedIn

    void checkIn();

public:
    BodyPipe &pipe;
    MemBuf &buf;
    const uint64_t offset; // of current content, relative to the body start

protected:
    const size_t checkedOutSize;
    bool checkedIn;

private:
    BodyPipeCheckout(const BodyPipeCheckout &); // prevent copying
    BodyPipeCheckout &operator =(const BodyPipeCheckout &); // prevent assignment
};

// Connects those who produces message body content with those who
// consume it. For example, connects ConnStateData with FtpStateData OR
// ICAPModXact with HttpStateData.
class BodyPipe: public RefCountable
{
public:
    typedef RefCount<BodyPipe> Pointer;
    typedef BodyProducer Producer;
    typedef BodyConsumer Consumer;
    typedef BodyPipeCheckout Checkout;

    enum { MaxCapacity = SQUID_TCP_SO_RCVBUF };

    friend class BodyPipeCheckout;

public:
    BodyPipe(Producer *aProducer);
    ~BodyPipe(); // asserts that producer and consumer are cleared

    void setBodySize(uint64_t aSize); // set body size
    bool bodySizeKnown() const { return theBodySize >= 0; }
    uint64_t bodySize() const;
    uint64_t consumedSize() const { return theGetSize; }
    bool productionEnded() const { return !theProducer; }

    // called by producers
    void clearProducer(bool atEof); // aborts or sends eof
    size_t putMoreData(const char *buf, size_t size);
    bool mayNeedMoreData() const { return !bodySizeKnown() || needsMoreData(); }
    bool needsMoreData() const { return bodySizeKnown() && unproducedSize() > 0; }
    uint64_t unproducedSize() const; // size of still unproduced data
    bool stillProducing(const Producer *producer) const { return theProducer == producer; }
    void expectProductionEndAfter(uint64_t extraSize); ///< sets or checks body size

    // called by consumers
    bool setConsumerIfNotLate(Consumer *aConsumer);
    void clearConsumer(); // aborts if still piping
    size_t getMoreData(MemBuf &buf);
    void consume(size_t size);
    bool expectMoreAfter(uint64_t offset) const;
    bool exhausted() const; // saw eof/abort and all data consumed
    bool stillConsuming(const Consumer *consumer) const { return theConsumer == consumer; }

    // start or continue consuming when there is no consumer
    void enableAutoConsumption();

    const MemBuf &buf() const { return theBuf; }

    const char *status() const; // for debugging only

protected:
    // lower-level interface used by Checkout
    MemBuf &checkOut(); // obtain raw buffer
    void checkIn(Checkout &checkout); // return updated raw buffer
    void undoCheckOut(Checkout &checkout); // undo checkout efffect

    void scheduleBodyDataNotification();
    void scheduleBodyEndNotification();

    // keep counters in sync and notify producer or consumer
    void postConsume(size_t size);
    void postAppend(size_t size);

    void startAutoConsumption(); // delayed start of enabled consumption

private:
    int64_t  theBodySize;   // expected total content length, if known
    Producer *theProducer; // content producer, if any
    Consumer *theConsumer; // content consumer, if any

    uint64_t thePutSize; // ever-increasing total
    uint64_t theGetSize; // ever-increasing total

    MemBuf theBuf; // produced but not yet consumed content, if any

    bool mustAutoConsume; // consume when there is no consumer
    bool isCheckedOut; // to keep track of checkout violations

    CBDATA_CLASS2(BodyPipe);
};

#endif /* SQUID_BODY_PIPE_H */
