
/* Copyright 2007-2010 United States Government as represented by the     */
/* Administrator of The National Aeronautics and Space Administration. All */
/* Rights Reserved.                                                        */


/** @file BaseConnection.h
 *
 *  @brief This file contains the base class for implementing middleware
 * connection wrappers.
 *
**/

#ifndef gmsec_internal_BaseConnection_h
#define gmsec_internal_BaseConnection_h

#include <string>

#include <gmsec/Status.h>
#include <gmsec/Connection.h>
#include <gmsec/util/Thread.h>
#include <gmsec/util/CountDownLatch.h>
#include <gmsec/internal/ci_less.h>
#include <gmsec/internal/TrackingDetails.h>
#include <gmsec/util/Condition.h>
#include <gmsec/util/shared_ptr.h>

// #include <list>


namespace gmsec
{

class Callback;
class Connection;
class ErrorCallback;
class Message;
class ReplyCallback;

namespace internal
{

class CallbackLookup;
class Dispatcher;
class GarbageCollector;
class RequestThread;
class TrackingDetails;



/** @class BaseConnection
 * @brief This is the base class for middleware connections. Each middleware connection
 * implements a class derived from BaseConnection to abstract middleware specific connection details.
 * The application will never access the middleware connection directly, but only through the gmsec::Connection "interface".
 *
 * The connection class provides services available on an implemented GMSEC connection. An application
 * can create multiple connection objects with different parameters and different middleware.
 * Creating multiple connections to the same middleware is not prevented but may not be supported
 * by all middleware implementations.
 *
 * Example creation and use:
 * @code
 * BaseConnection *conn = NULL;
 *
 * //Create config from command line arguments
 * Config cfg(argc,argv);
 *
 * //Create the BaseConnection
 * result = BaseConnectionFactory::Create(&cfg,conn);
 * if( result.isError() )
 *	//handle error
 *
 * //Establish the connection
 * result = conn->Connect();
 * if( result.isError() )
 *	//handle error
 * @endcode
 *
 * @sa BaseConnectionFactory @n
 *		Config
*/
class GMSEC_API BaseConnection
{
public:

#ifdef GMSEC_USE_ENUM_FOR_CONSTANTS
	enum
	{
		MIN_TIMEOUT_ms = 10,						// 0.01 s

		REPUBLISH_NEVER = -1,
		DEFAULT_REPUBLISH_ms = 60000,				// 1 minute
		MIN_REPUBLISH_ms = 100,						// 0.1 s
		PLACE_HOLDER
	};
#else
	static const int MIN_TIMEOUT_ms = 10;			// 0.01 s

	static const int REPUBLISH_NEVER = -1;
	static const int DEFAULT_REPUBLISH_ms = 60000;	// 1 minute
	static const int MIN_REPUBLISH_ms = 100;		// 0.1 s
#endif

	static const char *REPLY_UNIQUE_ID_FIELD;		// "REPLY-UNIQUE-ID"


	/** @fn BaseConnection(Config *config)
	 * @brief Construct a connection based upon paramaters set in the config object.
	 *
	 * @note This is never called by the client application, but is used by the API
	 * to handle configuration parameters that are common to all BaseConnection middleware
	 * implementations.
	 *
	 * @sa ConnectionFactory::Create(Config *cfg, Connection *&conn)
	 */
	BaseConnection(Config *config);
	/** @fn ~BaseConnection()
	 * @brief base class deconstructor
	 */
	virtual ~BaseConnection();

	/** @fn createExternal ()
	*/
	Connection *createExternal();

	static void destroy(BaseConnection *conn);

	virtual void shutdown();

	/** @fn Connect()
	 * @brief This function establishes this connection to the middleware
	 *
	 * @return status - result of the connection operation
	 */
	virtual Status CALL_TYPE Connect();

	/** @fn Disconnect()
	 * @brief This function terminates this connection to the middleware.
	 * It is automatically called by the destructor if necessary
	 *
	 * @return status - result of the connection operation
	 */
	virtual Status CALL_TYPE Disconnect();

	/** @fn IsConnected()
	 * @brief This function returns t/f whether the connection has
	 * been established
	 *
	 * @return true if connected
	 */
	bool IsConnected();

	/** @fn GetLibraryRootName()
	 * @brief This function identifies the root library name and therefore the
	 * connection type that this connection is associated with. For example,
	 * the root library name for the icsswb middleware library is "gmsec_icsswb"
	 * and matches the name of the windows library (gmsec_icsswb.dll) and the
	 * UNIX library (libgmsec_icsswb.so).
	 *
	 * @return root library name
	 *
	 * @sa Message::GetLibraryRootName()
	 */
	virtual const char * CALL_TYPE GetLibraryRootName() = 0;

	/** @fn GetLibraryVersion()
	 * @brief This function returns a string containing the version information for
	 * this connection's associated middleware.
	 *
	 * @return library version
	 */
	virtual const char * CALL_TYPE GetLibraryVersion() = 0;

	/** @fn RegisterErrorCallback(const char *event, ErrorCallback *cb)
	 * @brief This function allows the registration of a callback for a particular
	 * error event. Event names are middleware dependent.
	 *
	 * All connection types implement at least following error events:
	 *
	 * "CONNECTION_DISPATCHER_ERROR" - Auto-dispatcher error callback that gets called
	 *		whenever an error occurs inside the auto-dispatcher.
	 *
	 * "CONNECTION_REQUEST_TIMEOUT" - Request w/callback error callback that gets called
	 *		whenever an error occurs while trying to process an request (ex. timeout)
	 *
	 * @param event - name of event to register
	 * @param cb - object derrived from ErrorCallback to register for this error event
	 * @return status - result of the connection operation
	 *
	 * @sa ErrorCallback
	 */
	virtual Status CALL_TYPE RegisterErrorCallback(const char *event, ErrorCallback *cb);

	/** @fn Subscribe(const char *subject)
	 * @brief This function subscribes to a particular subject or pattern. This
	 * causes middleware routing of messages that match that subject or pattern
	 * be queued for this process. Messages that are subscribed to without callback
	 * need to be pulled from the queue using GetNextMsg() and are thrown away if
	 * the auto-dispatcher is used.
	 *
	 * Example subscription patterns:
	 * @code
	 * // this will match only messages with this exact subject
	 * conn->Subscribe("gmsec.mission.const.sat.evt.msg");
	 *
	 * // this will match messages with any mission
	 * conn->Subscribe("gmsec.*.const.sat.evt.msg");
	 *
	 * // this will match messages that have AT LEAST ONE MORE TAG
	 * //	(will not match "gmsec.mission.const.sat")
	 * conn->Subscribe("gmsec.mission.const.sat.>");
	 *
	 * // this will match any event message
	 * conn->Subscribe("gmsec.*.*.*.evt.>");
	 * @endcode
	 *
	 * @note Although subscription behavior is outlines as above, the actual behavior for a particular
	 * middleware implementation MAY deviate from this behavior slightly.
	 *
	 * @param subject - subject pattern to match received messages
	 * @return status - result of the connection operation
	 *
	 * @sa BaseConnection::GetNextMsg(Message *&msg, GMSEC_I32 timeout) @n
	 *     BaseConnection::StartAutoDispatch() @n
	 *     BaseConnection::StopAutoDispatch()
	 */
	virtual Status CALL_TYPE Subscribe(const char *subject) = 0;

	/** @fn Subscribe(const char *subject, Callback *cb)
	 * @brief This function subscribes to a particular subject or pattern and
	 * associates a callback to be called when messages matching the subject
	 * or pattern are received. If all subscriptions are performed using this
	 * function then the auto-dispatcher can be used to asynchronously receive
	 * messages. If GetNextMsg() is used to pull messages then DispatchMsg()
	 * will need to be called to ensure registered Callbacks are called.
	 *
	 * <b>see BaseConnection::Subscribe(const char *subject) for an explaination of subscription patterns</b>
	 *
	 * @param subject - subject pattern to match received messages
	 * @param cb - callback to be called when message is received
	 * @return status - result of the connection operation
	 *
	 * @sa BaseConnection::Subscribe(const char *subject) @n
	 *	   BaseConnection::GetNextMsg(Message *&msg, GMSEC_I32 timeout) @n
	 *     BaseConnection::DispatchMsg(Message *msg) @n
	 *     BaseConnection::StartAutoDispatch() @n
	 *     BaseConnection::StopAutoDispatch()
	 */
	virtual Status CALL_TYPE Subscribe(const char *subject, Callback *cb);

	/** @fn UnSubscribe(const char *subject)
	 * @brief This function unsubscribes to a particular subject pattern, and will stop the reception
	 * of messages that match this pattern. It will also remove the registration of any callbacks with
	 * this subject pattern.
	 *
	 * @param subject - subject pattern that was used to match received messages
	 * @return status - result of the connection operation
	 *
	 * @sa BaseConnection::Subscribe(const char *subject) @n
	 *	   BaseConnection::GetNextMsg(Message *&msg, GMSEC_I32 timeout) @n
	 *     BaseConnection::DispatchMsg(Message *msg) @n
	 *     BaseConnection::StartAutoDispatch() @n
	 *     BaseConnection::StopAutoDispatch()
	 */
	virtual Status CALL_TYPE UnSubscribe(const char *subject);
	/** @fn UnSubscribe(const char *subject, Callback *cb)
	 * @brief This function unsubscribes a single callback to a particular subject pattern, and
	 * will not unsubscribe the reception of the message. It will prevent a particular callback
	 * from being called by the auto-dispatch or DispatchMsg(), but the message will still be
	 * received for GetNextMsg().
	 *
	 * @param subject - subject pattern to match received messages
	 * @param cb - callback to be called when message is received
	 * @return status - result of the connection operation
	 *
	 * @sa BaseConnection::Subscribe(const char *subject) @n
	 *	   BaseConnection::GetNextMsg(Message *&msg, GMSEC_I32 timeout) @n
	 *     BaseConnection::DispatchMsg(Message *msg) @n
	 *     BaseConnection::StartAutoDispatch() @n
	 *     BaseConnection::StopAutoDispatch()
	 */
	virtual Status CALL_TYPE UnSubscribe(const char *subject, Callback *cb);


	/** @fn CreateMessage( Message *&msg )
	 * @brief This function creates a message for this particular middleware connection.
	 * The kind & subject are set to defaults dependent upon the particular middleware implementation.
	 *
	 * @param msg - Message pointer to be filled by created message
	 * @return status - result of the connection operation
	 *
	 */
	virtual Status CALL_TYPE CreateMessage(Message *&msg);

	/** @fn CreateMessage(const char *subject, GMSEC_MSG_KIND msgKind, Message *&msg)
	 * @brief This function creates a message for this particular middleware connection.
	 * The subject name for this call MUST be a valid subject name and NOT a pattern.
	 *
	 * @param subject - subject under which this message will eventually be published/requested
	 * @param msgKind - indentifier for the intended message kind
	 * @param msg - Message pointer to be filled by created message
	 * @return status - result of the connection operation
	 *
	 */
	virtual Status CALL_TYPE CreateMessage(const char *subject, GMSEC_MSG_KIND msgKind, Message *&msg) = 0;

	/** @fn CreateMessage(const char *subject, GMSEC_MSG_KIND msgKind, Message *&msg, Config *config)
	  * @brief The Config object can be used to set Subject, Kind or middleware specific settings.
	 * The options are specific to middleware implementation and are listed in the <i>GMSEC User's Guide</i>
	 *
	 * @param subject - subject under which this message will eventually be published/requested
	 * @param msgKind - indentifier for the intended message kind
	 * @param msg - Message pointer to be filled by created message
	 * @param config - Config object used to pass parameters to the middleware specific message implementation
	 * @return status - result of the connection operation
	 *
	 * @sa Connection::CreateMessage(const char *subject, GMSEC_MSG_KIND msgKind, Message *&msg)
	 */
	Status CALL_TYPE CreateMessage(const char *subject, GMSEC_MSG_KIND msgKind, Message *&msg, Config *config);

	/** @fn ConvertMessage( Message *in, Message *&out )
	 * @brief This function will call "CloneMessage" to copy a message to this connection
	 * from another connection, only if necessary. This is used by the API, or a client program
	 * to publish a message that was created or recieved on on middleware to another.
	 *
	 * @note ConvertCleanup() needs to be called with the same parameters, after the message is no
	 * longer needed, to ensure any memory required is cleaned up.
	 *
	 * @param in - message to convert FROM
	 * @param out - out parameter filled with the coverted message (could be the same if conversion wasn't necessary)
	 * @return status - result of the connection operation
	 *
	 * @sa BaseConnection::ConvertCleanup( Message *in, Message *out )
	 */
	virtual Status CALL_TYPE ConvertMessage(Message *in, Message *&out);

	/** @fn ConvertCleanup( Message *in, Message *out )
	 * @brief This function cleans up any memory allocated by ConvertMessage() when
	 * finished with the associated message. The parameters need to be
	 * exactly the same as those originally passed to ConvertMessage().
	 *
	 * @param in - message that was originally coverted FROM
	 * @param out - message that was output by the ConvertMessage() call
	 * @return status - result of the connection operation
	 *
	 * @sa BaseConnection::ConvertMessage( Message *in, Message *&out )
	 */
	virtual Status CALL_TYPE ConvertCleanup(Message *in, Message *out);

	/** @fn CloneMessage( Message *in, Message *&out )
	 * @brief This function copies a message without knowing what type it is. This
	 * function can be used to copy a message's contents.
	 *
	 * @note The 'out' message MUST BE CLEANED UP with DestroyMessage() by the client application.
	 *
	 * @param in - message to be cloned
	 * @param out - cloned message
	 * @return status - result of the connection operation
	 *
	 * @sa BaseConnection::DestroyMessage(Message *msg)
	 */
	virtual Status CALL_TYPE CloneMessage(Message *in, Message *&out);

	/** @fn DestroyMessage(Message *msg)
	 * @brief This function destroys a message and cleans up any associated memory.
	 * The base implementation just calls BaseMessage::destroy(msg);
	 * override to do more.
	 *
	 * @param msg - message to be destroyed
	 * @return status - result of the connection operation
	 */
	virtual Status CALL_TYPE DestroyMessage(Message *msg);

	/** @fn Publish(Message *msg)
	 * @brief This function will publish a message to the middleware.
	 *
	 * @param msg - message to be published
	 * @return status - result of the connection operation
	 */
	virtual Status CALL_TYPE Publish(Message *msg) = 0;

	/** @fn Request(Message *request, GMSEC_I32 timeout, Callback *cb, GMSEC_I32 republish_ms)
	 * @brief This function will send a request asyncronously. The callback will be called for the reply
	 * if it is received within the specified timeout. This function will not block.
	 * The timeout value is expressed in milliseconds.
	 *
	 * <B>IMPORTANT:</B> The request message passed into this function will be cleaned up when the processing
	 * is complete, therefore it must be created new and not stored or cleaned up by the client program.
	 *
	 * <B>NOTE:</B> This version, and the Callback class is DEPRECATED. Please use the ReplyCallback
	 * version of this function.
	 *
	 * @param request - message to be sent
	 * @param timeout - maximum time to wait for reply (in milliseconds)
	 * @param cb - Callback to call when reply is receieved
	 * @return status - result of the request operation
	 */
	virtual Status CALL_TYPE Request(Message *request, GMSEC_I32 timeout, Callback *cb, GMSEC_I32 republish_ms);

	/** @fn Request(Message *request, GMSEC_I32 timeout, ReplyCallback *cb, GMSEC_I32 republish_ms)
	 * @brief This function will send a request asyncronously. The callback will be called for the reply
	 * if it is received within the specified timeout. This function will not block.
	 * The timeout value is expressed in milliseconds.
	 *
	 * <B>IMPORTANT:</B> The request message passed into this function will be cleaned up when the processing
	 * is complete, therefore it must be created new and not stored or cleaned up by the client program.
	 *
	 * @param request - message to be sent
	 * @param timeout - maximum time to wait for reply (in milliseconds)
	 * @param cb - Callback to call when reply is receieved
	 * @return status - result of the request operation
	 */
	virtual Status CALL_TYPE Request(Message *request, GMSEC_I32 timeout, ReplyCallback *cb, GMSEC_I32 republish_ms);

	/** @fn Request(Message *request, GMSEC_I32 timeout, Message *&reply, GMSEC_I32 republish_ms)
	 * @brief This function will send a request, wait for the specified timeout, and return the received reply
	 * This function will block until the reply is received or the timeout is reached.
	 * The timeout value is expressed in milliseconds.
	 *
	 * @param request - message to be sent
	 * @param timeout - maximum time to wait for reply (in milliseconds)
	 * @param reply - out parameter reply message if received
	 * @return status - result of the request operation
	 */
	virtual Status CALL_TYPE Request(Message *request, GMSEC_I32 timeout, Message *&reply, GMSEC_I32 republish_ms);

	/** @fn Reply(Message *request,Message *reply)
	 * @brief This function will send a reply to a given request.
	 *
	 * @param request - the recieved request that we are responding to
	 * @param reply - the reply to be sent
	 * @return status - result of the reply operation
	 */
	virtual Status CALL_TYPE Reply(Message *request, Message *reply) = 0;

	/** @fn doReply(Message *request,Message *reply)
	 * @brief This function will send a reply to a given request.
	     *
	     * @param request - the recieved request that we are responding to
	     * @param reply - the reply to be sent
	     * @return status - result of the reply operation
	 */
	virtual Status CALL_TYPE doReply(Message *request, Message *reply);

	/** @fn sendRequest (Message *request, string &id)
	 * @brief Send the request with a unique ID.
	* The unique ID must be stored in the GMSEC_REPLY_UNIQUE_ID field
	* of the request and returned through the id reference.
	*
	* @param request - the request to send
	* @param id - reference through which to return the request's unique ID
	* @return status - result of the operation
	 */
	virtual Status CALL_TYPE sendRequest(Message *request, std::string &id) = 0;

	/** @fn StartAutoDispatch()
	 * @brief This function will start a thread that will dispatch messages asynchronously when they are received.
	 * If this is used, all subscriptions must be made with callbacks or the messages with be dropped.
	 * If GetNextMessage() is called while the auto-dispatcher is used, the behavior will be undesireable
	 * and undefined.
	 *
	 * @return status - result of the start operation
	 *
	 * @sa BaseConnection::Subscribe(const char *subject, Callback *cb)
	 */
	virtual Status CALL_TYPE StartAutoDispatch();

	/** @fn StopAutoDispatch()
	 * @brief This function will stop the auto dispatch thread.
	 *
	 * @return status - result of the stop operation
	 */
	virtual Status CALL_TYPE StopAutoDispatch();
	virtual Status CALL_TYPE StopAutoDispatch(bool waitForComplete);

	/** @fn GetNextMsg(Message *&msg, GMSEC_I32 timeout)
	 * @brief This function returns the next message received within the specified timeout.
	 * The received messages are determined by the %Subscribe() function(s), but
	 * %DispatchMsg() needs to be called messages received from this function to
	 * ensure all registered callbacks are executed. @n
	 * @n
	 * This function <b>MUST NOT BE USED</b> if the auto-dispatcher is being used.
	 *
	 * @param msg - out parameter, the next received message, if any
	 * @param timeout - the maximum time to block waiting for a message, in milliseconds
	 * @return status - result of the operation
	 *
	 * @sa BaseConnection::Subscribe(const char *subject) @n
	 *     BaseConnection::Subscribe(const char *subject, Callback *cb) @n
	 *     BaseConnection::DispatchMsg(Message *msg) @n
	 *     BaseConnection::StartAutoDispatch() @n
	 *     BaseConnection::StopAutoDispatch()
	 */
	virtual Status CALL_TYPE GetNextMsg(Message *&msg, GMSEC_I32 timeout) = 0;

	/** @fn DispatchMsg(Message *msg)
	 * @brief This function will cause the any callbacks that are registered with matching
	 * patterns to be called.
	 *
	 * @param msg - message to be dispatched
	 * @return status - result of the operation
	 *
	 * @sa BaseConnection::GetNextMsg(Message *&msg, GMSEC_I32 timeout)
	 */
	virtual Status DispatchMsg(Message *msg);

	/** @fn GetLastDispatcherStatus()
	 * @brief When running with the auto-dispatcher, it may be necessary to monitor the
	 * status as it runs within a seperate thread.  This method allows access to the
	 * last status error reported by the dispatcher.  Once the status is read, the
	 * status is cleared.
	 *
	 * @note Another way to be notified in the case of a dispatcher error is to register an
	 * error callback with the RegisterErrorCallback() function.
	 *
	 * @sa BaseConnection::StartAutoDispatch() @n
	 *     BaseConnection::StopAutoDispatch() @n
	 *	   BaseConnection::RegisterErrorCallack()
	 */
	virtual Status GetLastDispatcherStatus();


	/** @fn GetName()
	 * @brief Get the logical name of this connection, if one has been assigned.
	 *	This is usefull for identifying connections within a client program.
	 *
	 * @return name of this connection
	 */
	virtual const char * GetName();

	/** @fn SetName( const char *name )
	 * @brief Set the logical name of this connection. This can be usedull for
	 * Identifying connections withing a client program.
	 *
	 * @param name - name of this connection
	 */
	virtual void SetName(const char *name);

	/** @fn GetConnectionGUI()
	 * @brief Get the string GUID for this connection.
	 *
	 */
	const char *GetConnectionGUI();

	/* helper to dispatch errors */
	Status DispatchError(const char *name, Message *msg, Status *status);

	/*
	** these are for C API support ONLY
	*/
	virtual Status RegisterErrorCallback(const char *event, GMSEC_ERROR_CALLBACK *cb);
	virtual Status Subscribe(const char *subject, GMSEC_CALLBACK *cb);
	virtual Status UnSubscribe(const char *subject, GMSEC_CALLBACK *cb);
	virtual Status Request(Message *request, GMSEC_I32 timeout, GMSEC_CALLBACK *cb);
	virtual Status Request(Message *request, GMSEC_I32 timeout, GMSEC_REPLY_CALLBACK *cb, GMSEC_ERROR_CALLBACK *er);

	// MW-INFO support
	virtual const char * CALL_TYPE GetMWINFO()
	{
		return GetLibraryRootName();
	}

	// Returns number of seconds and milliseconds since January 1, 1970
	static double GetSeconds();

#ifdef GMSEC_EXPOSE_CONNJUNK

	/* support function to simulate blocking wait */
	static GMSEC_I32 fakeBlockingReadPause();

	/* Support function to do a Pause for specified time period (millisecs) */
	static GMSEC_I32 fakeBlockingReadPause(GMSEC_I32 waitFor);
#endif

	/** @fn GetMWINFO(char *)
	 *  @brief Thread Safe alternative method for const char * GetMWINFO().
	 *
	 *  @param infoBuffer - user allocated buffer to store the mw information
	 *  @return char pointer to infoBuffer
	 */
	virtual char * CALL_TYPE GetMWINFO(char * infoBuffer);

protected:

	virtual TrackingDetails *getTracking();

	virtual bool onReply(Message *reply);


	/** @fn GetTime(char * dateBuffer)
	 * @brief This function will return the current GMT time/date in GMSEC standard format
	 *
	 * Example format:
	 * @code
	 * 2004-314-18:56:57.350
	 * @endcode
	 *
	 * @param dateBuffer - user allocated buffer to store the mw information
	 *
	 * @return current date/time in GMSEC standard format
	* OBSOLETE: please use timeutil::formatTime*
	 */
	// static char * GetTime(char * dateBuffer);


	/* Add support for API added fields */
	bool InsertTrackingFields(BaseMessage *msg);

	/* get computer name */
	// const char *GetCompName();

	/* instance counter */
	static GMSEC_U32 fInstanceCount;
	GMSEC_U32 fConnectionID;

	/* has connection been made? */
	volatile bool fConnectFlag;

	/* keep track of number of messages sent */
	GMSEC_U32 msg_counter;

	/* BaseConnection UID */
	std::string m_uid;

private:

	void initializeTracking(Config *config);
	void initializeRequest(Config *config);

	void resolveRequestTimeout(GMSEC_I32 &timeout_ms);
	void resolveRepublishInterval(GMSEC_I32 &republish_ms);

	bool ensureRequestThread();

	/** @fn shutdownAutoDispatch()
	 * @brief This function shuts down the auto-dispatch thread if
	 * necessary.  It is basically a checked StopAutoDispatch.
	 *
	 * @return bool - true if StopAutoDispatch was necessary
	 */
	bool shutdownAutoDispatch();

	/** @fn shutdownRequestThread()
	 * @brief This function shuts down the request thread if
	 * necessary.
	 *
	 * @return bool - true if the request thread needed to be shut down.
	 */
	bool shutdownRequestThread();

	/* the public interface object */
	gmsec::Connection *external;

	/* Connection name */
	std::string m_name;
	/* Machine name */
	std::string m_machine;
	/* User name */
	std::string m_userName;

	/* Reference to the dispatcher object used for auto dispatch */
	Dispatcher *fDispatcher;
	gmsec::util::shared_ptr<gmsec::util::Thread> fSharedDispatcher;

	/* Callback lookup container */
	CallbackLookup *fCallbackLkp;

	/* ErrorCallback lookup container */
	typedef std::map<std::string, ErrorCallback *, ci_less> errorCbLookup;
	typedef errorCbLookup::const_iterator errorCbLkpItr;

	errorCbLookup fErrorCbLkps;

	RequestThread *fRequestThread;
	gmsec::util::shared_ptr<gmsec::util::Thread> fSharedRequestThread;
	GMSEC_I32 fDefaultRepublish_ms;

	TrackingDetails fTracking;

	// Callback wrappers management and cleanup
	GarbageCollector *fCollector;

};



/**
* @class ConnectionBuddy
* @brief Provides exchange between internal and internal messages.
* Unfortunately, gcc 3.2.3 (maybe others) would not allow this to be a
* nested in BaseConnection and have its friend of Connection privileges.
*/
class GMSEC_API ConnectionBuddy
{
public:
	ConnectionBuddy(BaseConnection *internal)
	{
		connection.ptr = internal;
	}

	~ConnectionBuddy()
	{
		connection.ptr = NULL;
	}

	static Connection *createExternal(BaseConnection *internal);

	Connection *ptr()
	{
		return &connection;
	}


	static void destroy(Connection *conn);

private:
	Connection connection;
};


} // namespace internal
} // namespace gmsec

#endif  // gmsec_internal_BaseConnection_h

