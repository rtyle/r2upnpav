/// \file
/// \brief Definition of r2upnpav program

#include <cstdarg>
#include <iostream>
#include <iomanip>
#include <map>

// boost program options (link requires boost program_options library)
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>

#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>

#include <fcntl.h>

#include <libgupnp/gupnp-control-point.h>
#include <libgupnp-av/gupnp-av.h>

#include <lirc_client.h>

#include <cec.h>

#include "SystemException.h"

/// An Output object is created to handle all UPnP AV state and output
class Output {
private:
    /// LastChangeParser singleton instance parses every LastChange XML
    class LastChangeParser {
    private:
	static LastChangeParser *	instance;
	GUPnPLastChangeParser *		lastChangeParser;
	LastChangeParser() : lastChangeParser(gupnp_last_change_parser_new()) {}
    public:
	static LastChangeParser * getInstance() {
	    return instance ? instance : (instance = new LastChangeParser());
	}
	gboolean parseLastChange(
	    guint		instance,
	    char const *	lastChangeXml,
	    GError **		error,
	    ...)
	{
	    std::va_list args;
	    va_start(args, error);
	    gboolean result = gupnp_last_change_parser_parse_last_change_valist(
		lastChangeParser, instance, lastChangeXml, error, args);
	    va_end(args);
	    return result;
	}
    };
    /// An AVTransportService is created for each matching renderer
    class AVTransportService {
    private:
	size_t			verbose;
	std::string		name;
	GUPnPServiceProxy *	proxy;
	void operate(char const * operation) {
	    if (verbose) {
		std::cout << name << ": " << operation << std::endl;
	    }
	    GError * error = 0;
	    gupnp_service_proxy_send_action(proxy,
		operation, &error,
		// in NULL terminated argument 3-tuples
		"InstanceID",		G_TYPE_UINT,	0,
		NULL,
		// out NULL terminated argument 3-tuples
		NULL);
	    if (error) {
		boost::shared_ptr<GError> errorFree(error, g_error_free);
		std::cerr << name << ": " << operation << ": "
		    << error->message << std::endl;
	    }
	}
    public:
	AVTransportService(
	    size_t		verbose_,
	    char const *	name_,
	    GUPnPDeviceInfo *	mediaRendererDeviceInfo)
	:
	    verbose(verbose_),
	    name(name_),
	    proxy(GUPNP_SERVICE_PROXY(gupnp_device_info_get_service(
		mediaRendererDeviceInfo,
		"urn:schemas-upnp-org:service:AVTransport:1")))
	{
	}
	void pause()	{operate("Pause");}
	void previous()	{operate("Previous");}
	void next()	{operate("Next");}
	void play() {
	    if (verbose) {
		std::cout << name << ": Play" << std::endl;
	    }
	    GError * error = 0;
	    gupnp_service_proxy_send_action(proxy,
		"Play", &error,
		// in NULL terminated argument 3-tuples
		"InstanceID",		G_TYPE_UINT,	0,
		"Speed",		G_TYPE_STRING,	"1",
		NULL,
		// out NULL terminated argument 3-tuples
		NULL);
	    if (error) {
		boost::shared_ptr<GError> errorFree(error, g_error_free);
		std::cerr << name << ": Play: "
		    << error->message << std::endl;
	    }
	}
    };
    /// A RenderingControlService is created for each matching renderer
    class RenderingControlService {
    private:
	size_t			verbose;
	std::string		name;
	GUPnPServiceProxy *	proxy;
	gboolean		mute;
	guint			volume;
	void onLastChange(
	    char const *	notification,
	    GValue *		lastChange)
	{
	    GError *		error = 0;
	    char const *	lastChangeXml = g_value_get_string(lastChange);
	    // look for val's of instance 0 variables of interest,
	    // parse their formatted values and remember them.
	    // if any of these variables were not just (last) changed
	    // (e.g, mute won't change volume and vice-versa),
	    // it will not be reported and the parse will silently succeed
	    // without modifying the remembered values.
	    if (LastChangeParser::getInstance()->parseLastChange(
		    0,			// instance id of interest
		    lastChangeXml,	// XML to parse
		    &error,		// error returned
		    "Mute",	G_TYPE_BOOLEAN,	&mute,
		    "Volume",	G_TYPE_UINT,	&volume,
		    NULL)) {
		if (verbose) {
		    std::cout << name << ": LastChange "
			<< mute << " " << std::dec << volume << std::endl;
		}
	    } else if (error) {
		boost::shared_ptr<GError> errorFree(error, g_error_free);
		std::cerr << name << ": LastChange error: "
		    << error->message << std::endl;
	    }
	}
	static void onLastChangeThat(
	    GUPnPServiceProxy *	proxy,
	    char const *	name,
	    GValue *		lastChange,
	    gpointer		that)
	{
	    static_cast<RenderingControlService *>(that)
		->onLastChange(name, lastChange);
	}
    public:
	RenderingControlService(
	    size_t		verbose_,
	    char const *	name_,
	    GUPnPDeviceInfo *	mediaRendererDeviceInfo)
	:
	    verbose(verbose_),
	    name(name_),
	    proxy(GUPNP_SERVICE_PROXY(gupnp_device_info_get_service(
		mediaRendererDeviceInfo,
		"urn:schemas-upnp-org:service:RenderingControl:1"))),
	    mute(FALSE),
	    volume(0)
	{
	    gupnp_service_proxy_add_notify(proxy,
		"LastChange",
		G_TYPE_STRING,
		onLastChangeThat,
		this);
	    gupnp_service_proxy_set_subscribed(proxy, true);
	    GError * error;
	    // get the latest mute and volume settings.
	    // we may get (redundant) LastChange notification while we
	    // do this but we want to make sure we got it.
	    error = 0;
	    gupnp_service_proxy_send_action(proxy,
		"GetMute", &error,
		// in NULL terminated argument 3-tuples
		"InstanceID",		G_TYPE_UINT,	0,
		"Channel",		G_TYPE_STRING,	"Master",
		NULL,
		// out NULL terminated argument 3-tuples
		"CurrentMute",		G_TYPE_BOOLEAN,	&mute,
		NULL);
	    if (error) {
		boost::shared_ptr<GError> errorFree(error, g_error_free);
		std::cerr << name << ": GetMute error: "
		    << error->message << std::endl;
	    } else {
		if (verbose) {
		    std::cout << name << ": GetMute: "
			<< static_cast<bool>(mute) << std::endl;
		}
	    }
	    error = 0;
	    gupnp_service_proxy_send_action(proxy,
		"GetVolume", &error,
		// in NULL terminated argument 3-tuples
		"InstanceID",		G_TYPE_UINT,	0,
		"Channel",		G_TYPE_STRING,	"Master",
		NULL,
		// out NULL terminated argument 3-tuples
		"CurrentVolume",	G_TYPE_UINT,	&volume,
		NULL);
	    if (error) {
		boost::shared_ptr<GError> errorFree(error, g_error_free);
		std::cerr << name << ": GetVolume error: "
		    << error->message << std::endl;
	    } else {
		if (verbose) {
		    std::cout << name << ": GetVolume: "
			<< std::dec << volume << std::endl;
		}
	    }
	}
	void toggleMute() {
	    if (verbose) {
		std::cout << name << ": SetMute "
		    << static_cast<bool>(!mute) << std::endl;
	    }
	    GError * error = 0;
	    gupnp_service_proxy_send_action(proxy,
		"SetMute", &error,
		// in NULL terminated argument 3-tuples
		"InstanceID",		G_TYPE_UINT,	0,
		"Channel",		G_TYPE_STRING,	"Master",
		"DesiredMute",		G_TYPE_BOOLEAN,	!mute,
		NULL,
		// out NULL terminated argument 3-tuples
		NULL);
	    if (error) {
		boost::shared_ptr<GError> errorFree(error, g_error_free);
		std::cerr << name << ": SetMute error: "
		    << error->message << std::endl;
	    } else {
		mute = !mute;
	    }
	}
	void setRelativeVolume(gint adjustment) {
	    if (mute) {
		toggleMute();
	    }
	    if (verbose) {
		std::cout << name << ": SetRelativeVolume "
		    << std::dec << adjustment << std::endl;
	    }
	    GError * error = 0;
	    gupnp_service_proxy_send_action(proxy,
		"SetRelativeVolume", &error,
		// in NULL terminated argument 3-tuples
		"InstanceID",		G_TYPE_UINT,	0,
		"Channel",		G_TYPE_STRING,	"Master",
		"Adjustment",		G_TYPE_INT,	adjustment,
		NULL,
		// out NULL terminated argument 3-tuples
		"NewVolume",		G_TYPE_UINT,	&volume,
		NULL);
	    if (error) {
		boost::shared_ptr<GError> errorFree(error, g_error_free);
		std::cerr << name << ": SetVolume error: "
		    << error->message << std::endl;
	    } else {
		if (verbose) {
		    std::cout << name << ": SetVolume "
			<< std::dec << volume << std::endl;
		}
	    }
	}
    };

    typedef boost::shared_ptr<AVTransportService>
					AVTransportServicePointer;
    typedef std::map<std::string, AVTransportServicePointer>
					AVTransportServiceMap;
    typedef boost::shared_ptr<RenderingControlService>
					RenderingControlServicePointer;
    typedef std::map<std::string, RenderingControlServicePointer>
					RenderingControlServiceMap;
    size_t				verbose;
    boost::regex			match;
    AVTransportServiceMap		avTransportServiceMap;
    RenderingControlServiceMap		renderingControlServiceMap;

    void deviceProxyAvailable(
        GUPnPControlPoint *	controlPoint,
        GUPnPDeviceProxy *	mediaRendererDevice)
    {
	GUPnPDeviceInfo * mediaRendererDeviceInfo
	    = GUPNP_DEVICE_INFO(mediaRendererDevice);
	char const * name
	    = gupnp_device_info_get_friendly_name(mediaRendererDeviceInfo);
	if (boost::regex_match(name, match)) {
	    if (verbose) {
		std::cout << "renderer available match:\t"
		    << name << std::endl;
	    }
	    if (avTransportServiceMap.end()
		    == avTransportServiceMap.find(name)) {
		// construct a new AVTransportService
		AVTransportServicePointer avTransportServicePointer(
		    new AVTransportService(
			verbose, name, mediaRendererDeviceInfo));
		// don't add it to the map until after it is fully constructed
		// to prevent premature callbacks
		avTransportServiceMap[name]
		    = avTransportServicePointer;
	    }
	    if (renderingControlServiceMap.end()
		    == renderingControlServiceMap.find(name)) {
		// construct a new RenderingControlService
		RenderingControlServicePointer renderingControlServicePointer(
		    new RenderingControlService(
			verbose, name, mediaRendererDeviceInfo));
		// don't add it to the map until after it is fully constructed
		// to prevent premature callbacks
		renderingControlServiceMap[name]
		    = renderingControlServicePointer;
	    }
	} else {
	    if (verbose) {
		std::cout << "renderer available mismatch:\t"
		    << name << std::endl;
	    }
	}
    }
    static void deviceProxyAvailableThat(
	GUPnPControlPoint *	controlPoint,
	GUPnPDeviceProxy *	mediaRendererDevice,
	gpointer		that)
    {
	static_cast<Output *>(that)->deviceProxyAvailable(
	    controlPoint, mediaRendererDevice);
    }
    void deviceProxyUnavailable(
	GUPnPControlPoint *	controlPoint,
	GUPnPDeviceProxy *	mediaRendererDevice)
    {
	GUPnPDeviceInfo * mediaRendererDeviceInfo
	    = GUPNP_DEVICE_INFO(mediaRendererDevice);
	char const * name
	    = gupnp_device_info_get_friendly_name(mediaRendererDeviceInfo);
	if (verbose) {
	    std::cout << "renderer unavailable:\t" << name << std::endl;
	}
	{
	    AVTransportServiceMap::iterator it
		= avTransportServiceMap.find(name);
	    if (avTransportServiceMap.end() != it) {
		avTransportServiceMap.erase(it);
	    }
	}
	{
	    RenderingControlServiceMap::iterator it
		= renderingControlServiceMap.find(name);
	    if (renderingControlServiceMap.end() != it) {
		renderingControlServiceMap.erase(it);
	    }
	}
    }
    static void deviceProxyUnavailableThat(
	GUPnPControlPoint *	controlPoint,
	GUPnPDeviceProxy *	mediaRendererDevice,
	gpointer		that)
    {
	static_cast<Output *>(that)->deviceProxyUnavailable(
	    controlPoint, mediaRendererDevice);
    }
public:
    Output(
	size_t		verbose_,
	char const *	interface,
	unsigned int	port,
	std::string	match_)
    throw(std::runtime_error)
    :
	verbose(verbose_),
	match(match_),
	avTransportServiceMap(),
	renderingControlServiceMap()
    {
	GError * error = 0;
	GUPnPContext * context = gupnp_context_new (
	    NULL,	// GMainContext *
	    interface,	// network interface
	    port,	// TCP SOAP server (listening) port. 0 => any port
	    &error);
	if (error) {
	    boost::shared_ptr<GError> errorFree(error, g_error_free);
	    throw std::runtime_error(error->message);
	}
	GUPnPControlPoint * controlPoint = gupnp_control_point_new(
	    context,
	    "urn:schemas-upnp-org:device:MediaRenderer:1");
	g_signal_connect(
	    controlPoint,
	    "device-proxy-available",
	    reinterpret_cast<GCallback>(deviceProxyAvailableThat),
	    this);
	g_signal_connect(
	    controlPoint,
	    "device-proxy-unavailable",
	    reinterpret_cast<GCallback>(deviceProxyUnavailableThat),
	    this);
	gssdp_resource_browser_set_active(
	    GSSDP_RESOURCE_BROWSER(controlPoint), true);
    }
    void pause() {
	for (AVTransportServiceMap::iterator it = avTransportServiceMap.begin();
		avTransportServiceMap.end() != it; ++it) {
	    it->second.get()->pause();
	}
    }
    void play() {
	for (AVTransportServiceMap::iterator it = avTransportServiceMap.begin();
		avTransportServiceMap.end() != it; ++it) {
	    it->second.get()->play();
	}
    }
    void previous() {
	for (AVTransportServiceMap::iterator it = avTransportServiceMap.begin();
		avTransportServiceMap.end() != it; ++it) {
	    it->second.get()->previous();
	}
    }
    void next() {
	for (AVTransportServiceMap::iterator it = avTransportServiceMap.begin();
		avTransportServiceMap.end() != it; ++it) {
	    it->second.get()->next();
	}
    }
    void setRelativeVolume(int adjustment) {
	for (RenderingControlServiceMap::iterator it
		    = renderingControlServiceMap.begin();
		renderingControlServiceMap.end() != it; ++it) {
	    it->second.get()->setRelativeVolume(adjustment);
	}
    }
    void toggleMute() {
	for (RenderingControlServiceMap::iterator it
		    = renderingControlServiceMap.begin();
		renderingControlServiceMap.end() != it; ++it) {
	    it->second.get()->toggleMute();
	}
    }
};
Output::LastChangeParser * Output::LastChangeParser::instance = 0;

/// An LircInput object is created to handle all LIRC daemon input
class LircInput {
private:
    // A Connection is a LIRC client connection to the LIRC daemon (lircd)
    class Connection {
    private:
	int fd;
    public:
	Connection(size_t verbose, char const * program)
	throw(boost::system::system_error)
	{
	    try {
		fd = SystemException::throwErrorIfNegative1(
		    lirc_init(const_cast<char *>(program), verbose));
	    } catch (std::exception & e) {
		std::cerr << "\tlircd connection failed" << std::endl;
		throw;
	    }
	    SystemException::throwErrorIfNegative1(
		fcntl(fd, F_SETFL, O_NONBLOCK
		    | SystemException::throwErrorIfNegative1(
			fcntl(fd, F_GETFL, 0))));
	}
	operator int() const {return fd;}
	~Connection() {lirc_deinit();}
    };
    class Config {
    private:
	lirc_config *	config;
    public:
	Config(char const * lircrc) throw(boost::system::system_error) {
	    try {
		SystemException::throwErrorIfNegative1(
		    lirc_readconfig(const_cast<char *>(lircrc), &config, 0));
	    } catch (std::exception & e) {
		std::cerr << "\tlircrc read failed" << std::endl;
		throw;
	    }
	}
	operator lirc_config * () {return config;}
	~Config() {lirc_freeconfig(config);}
    };
    class Channel {
    private:
	GIOChannel * channel;
    public:
	Channel(int fd) : channel(g_io_channel_unix_new(fd)) {}
	operator GIOChannel * () const {return channel;}
	~Channel() {g_io_channel_unref(channel);}
    };
    size_t				verbose;
    Connection				connection;
    Config				config;
    Channel				channel;
    boost::shared_ptr<GMainLoop>	loop;
    Output &				output;
    gboolean input(
	GIOChannel *	source,
	GIOCondition	condition)
    {
	try {
	    // batch up operations
	    int play			= 0;
	    int next			= 0;
	    int volumeAdjustment	= 0;
	    bool toggleMute		= false;
	    while (true) {
		char * code;
		SystemException::throwErrorIfNegative1(
		    lirc_nextcode(&code));
	    if (!code) break; // no more codes at this time
		boost::shared_ptr<char> codeFree(code, free);
		if (verbose) {
		    std::cout << "\tlircd code:\t"<< code;
		}
		while (true) {
		    char * operation;
		    SystemException::throwErrorIfNegative1(
			lirc_code2char(config, code, &operation));
		if (!operation) break;	// no more operations for this event
		    if (verbose) {
			std::cout << "\tlircrc config:\t"
			    << operation << std::endl;
		    }
		    if (0 == strcasecmp("Pause", operation)) {
			--play;
		    } else if (0 == strcasecmp("Play", operation)) {
			++play;
		    } else if (0 == strcasecmp("Previous", operation)) {
			--next;
		    } else if (0 == strcasecmp("Next", operation)) {
			++next;
		    } else if (0 == strcasecmp("VolumeUp", operation)) {
			++volumeAdjustment;
		    } else if (0 == strcasecmp("VolumeDown", operation)) {
			--volumeAdjustment;
		    } else if (0 == strcasecmp("Mute", operation)) {
			toggleMute = !toggleMute;
		    } else {
			std::cerr << "\tlircrc config:\t"
			    << operation << ": unsupported" << std::endl;
		    }
		}
	    }
	    // perform batched up operations
	    if (play) {
		0 > play ? output.pause() : output.play();
	    }
	    if (next) {
		0 > next ? output.previous() : output.next();
	    }
	    if (volumeAdjustment) {
		output.setRelativeVolume(volumeAdjustment);
	    }
	    if (toggleMute) {
		output.toggleMute();
	    }
	} catch (boost::system::system_error & e) {
	    std::cerr << e.what() << std::endl;
	    if (boost::system::errc::resource_unavailable_try_again
		    == e.code()) {
		std::cerr << "\tlircd connection dropped, exiting" << std::endl;
		g_main_loop_quit(loop.get());
	    }
	}
	return true;
    }
    static gboolean inputThat(
	GIOChannel *	source,
	GIOCondition	condition,
	gpointer	that)
    {
	return static_cast<LircInput *>(that)->input(source, condition);
    }
public:
    LircInput(
	size_t				verbose_,
	char const *			program,
	char const *			lircrc,
	boost::shared_ptr<GMainLoop>	loop_,
	Output &			output_)
    throw(boost::system::system_error)
    :
	verbose(verbose_),
	connection(verbose, program),
	config(lircrc),
	channel(connection),
	loop(loop_),
	output(output_)
    {
	g_io_add_watch(channel, G_IO_IN, inputThat, this);
    }
};

/// A CecInput object is created to handle all CEC input
class CecInput {
private:
    int logMessage(CEC::cec_log_message const & m) {
	if (verbose) {
	    std::cout << "\tCEC log\t";
	    // m.level's are, apparently, mutually exclusive bits in a mask
	    // but we'll treat them as independent bits here, just in case
	    if (m.level & CEC::CEC_LOG_ERROR)	std::cout << 'E';
	    if (m.level & CEC::CEC_LOG_WARNING)	std::cout << 'W';
	    if (m.level & CEC::CEC_LOG_NOTICE)	std::cout << 'N';
	    if (m.level & CEC::CEC_LOG_TRAFFIC)	std::cout << 'T';
	    if (m.level & CEC::CEC_LOG_DEBUG)	std::cout << 'D';
	    std::cout
		<< "\t" << std::dec << m.time
		<< "\t" << m.message
		<< std::endl;
	}
	return 0;
    }
    static int logMessageThat(void * that, CEC::cec_log_message const m) {
	return static_cast<CecInput *>(that)->logMessage(m);
    }
    int keyPress(CEC::cec_keypress const & k) {
	// a key press is the result of bus traffic (which may be logged)
	// that includes a command that suggests such.
	// such commands will be directed to us if appropriate for our type
	// and the current CEC source selection.
	// regardless of the current CEC source selection,
	// an audio system will get volume up/down and mute key presses.
	//
	// duration is 0 when key goes down
	// and 0 for repeating keys (e.g. volume) while it is held down.
	// when key is released, duration is the number of milliseconds since
	// the last keypress event (which is short (~100) for repeating keys).
	if (verbose) {
	    std::cout << "\tCEC key"
		<< "\t" << std::dec << k.duration
		<< "\t" << std::hex << std::setw(2) << std::setfill('0')
		    << k.keycode
		<< "\t" << adapter.get()->ToString(k.keycode)
		<< std::endl;
	}
	if (0 == k.duration) {
	    try {
		// we don't handle the operation here;
		// rather we forward the keycode over a pipe
		// to be handled where it must be: in the UPnP thread.
		SystemException::throwErrorIfNegative1(
		    write(pipe.fds[1], &k.keycode, sizeof k.keycode));
	    } catch (boost::system::system_error & e) {
		std::cerr << e.what() << std::endl;
		close(pipe.fds[1]);
		pipe.fds[1] = -1;
	    }
	}
	return 0;
    }
    static int keyPressThat(void * that, CEC::cec_keypress const k) {
	return static_cast<CecInput *>(that)->keyPress(k);
    }
    int command(CEC::cec_command const & c) {
	if (verbose) {
	    std::cout << "\tCEC cmd"
		<< "\t" << adapter.get()->ToString(c.initiator)
		<< "\t" << adapter.get()->ToString(c.destination)
		<< "\t" << static_cast<bool>(c.ack)
		<< "\t" << static_cast<bool>(c.eom)
		<< "\t" << std::dec << c.transmit_timeout;
	    if (c.opcode_set) {
		std::cout
		    << "\t" << adapter.get()->ToString(c.opcode)
		    << "\t";
		for (size_t i = 0; i < c.parameters.size; ++i) {
		    std::cout << std::hex << std::setw(2) << std::setfill('0')
			<< c.parameters.data[i]; 
		}
	    }
	    std::cout << std::endl;
	}
	return 0;
    }
    static int commandThat(void * that, CEC::cec_command const c) {
	return static_cast<CecInput *>(that)->command(c);
    }
    int alert(CEC::libcec_alert const & a, CEC::libcec_parameter const & p) {
	std::cerr << "\tCEC alert\t";
	// no ToString(CEC::libcec_alert)
	switch (a) {
	    case CEC::CEC_ALERT_SERVICE_DEVICE:
		std::cerr << "service device"; break;
	    case CEC::CEC_ALERT_CONNECTION_LOST:
		std::cerr << "connection lost"; break;
	    case CEC::CEC_ALERT_PERMISSION_ERROR:
		std::cerr << "permission error"; break;
	    case CEC::CEC_ALERT_PORT_BUSY:
		std::cerr << "port busy"; break;
	    case CEC::CEC_ALERT_PHYSICAL_ADDRESS_ERROR:
		std::cerr << "physical address error"; break;
	}
	if (CEC::CEC_PARAMETER_TYPE_STRING == p.paramType) {
	    std::cerr << "\t" << static_cast<char *>(p.paramData);
	}
	std::cerr << std::endl;
	if (CEC::CEC_ALERT_CONNECTION_LOST == a) {
	    close(pipe.fds[1]);
	    pipe.fds[1] = -1;
	}
	return 0;
    }
    static int alertThat(void * that,
	    CEC::libcec_alert const a, CEC::libcec_parameter const p) {
	return static_cast<CecInput *>(that)->alert(a, p);
    }
    class Adapter {
    public:
	class Configuration : public CEC::libcec_configuration {
	public:
	    class CecInputCallbacks : public CEC::ICECCallbacks {
	    public:
		CecInputCallbacks() {
		    // Configuration::callbackParam must be that CecInput so
		    // *That class methods can call * instance methods for that
		    CBCecLogMessage	= CecInput::logMessageThat;
		    CBCecKeyPress	= CecInput::keyPressThat;
		    CBCecCommand	= CecInput::commandThat;
		    CBCecAlert		= CecInput::alertThat;
		}
	    };
	private:
	    CecInputCallbacks cecInputCallbacks;
	public:
	    Configuration(
		void *		cecInput,
		char const *	name)
	    :
		cecInputCallbacks()
	    {
		callbackParam = cecInput;
		callbacks = &cecInputCallbacks;
		clientVersion = CEC::LIBCEC_VERSION_CURRENT;
		strncpy(strDeviceName, name, sizeof(strDeviceName) - 1);
		deviceTypes.Add(CEC::CEC_DEVICE_TYPE_AUDIO_SYSTEM);
		bActivateSource = 0;
	    }
	};
	Configuration configuration;
	boost::shared_ptr<CEC::ICECAdapter> adapter;
	CEC::ICECAdapter * get() {return adapter.get();}
	Adapter(
	    void *		cecInput,
	    char const *	name,
	    char const *	port,
	    uint32_t		timeout)
	throw(std::runtime_error)
	:
	    configuration(cecInput, name),
	    adapter(
		static_cast<CEC::ICECAdapter *>(CECInitialise(&configuration)),
		CECDestroy)
	{
	    if (!get()) {
		throw std::runtime_error("CECInitialize failed");
	    }
	    get()->InitVideoStandalone();
	    CEC::cec_adapter adapters[10];
	    if (!port && 0 < get()->FindAdapters(adapters, 10, 0)) {
		port = adapters[0].comm;
	    }
	    if (!get()->Open(port, timeout)) {
		throw std::runtime_error("CEC::ICECAdapter::Open failed");
	    }
	}
	~Adapter() {
	    if (get()) {
		get()->Close();
	    }
	}
    };
    class Pipe {
    public:
	int fds[2];
	Pipe() throw(std::runtime_error) : fds{-1, -1} {
	    SystemException::throwErrorIfNegative1(pipe2(fds, O_CLOEXEC));
	    SystemException::throwErrorIfNegative1(
		fcntl(fds[0], F_SETFL, O_NONBLOCK
		    | SystemException::throwErrorIfNegative1(
			fcntl(fds[0], F_GETFL, 0))));
	}
	~Pipe() {
	    close(fds[0]);
	    close(fds[1]);
	}
    };
    class Channel {
    private:
	GIOChannel * channel;
    public:
	Channel(int fd) : channel(g_io_channel_unix_new(fd)) {}
	operator GIOChannel * () const {return channel;}
	~Channel() {g_io_channel_unref(channel);}
    };
    gboolean input(
	GIOChannel *	source,
	GIOCondition	condition)
    {
	// batch up operations
	int play		= 0;
	int next		= 0;
	int volumeAdjustment	= 0;
	bool toggleMute		= false;
	try {
	    while (true) {
		CEC::cec_user_control_code queue[4096];
		ssize_t length = SystemException::throwErrorIfNegative1(
		    read(pipe.fds[0], &queue, sizeof queue));
		if (0 == length) {
		    std::cerr << "\tCEC pipe closed, exiting" << std::endl;
		    g_main_loop_quit(loop.get());
		} else {
		    for (CEC::cec_user_control_code const * c = queue;
			    0 < length; ++c, length -= sizeof(*c)) {
			switch (*c) {
			    case CEC::CEC_USER_CONTROL_CODE_PLAY:
				++play; break;
			    case CEC::CEC_USER_CONTROL_CODE_PAUSE:
				--play; break;
			    case CEC::CEC_USER_CONTROL_CODE_FORWARD:
				++next; break;
			    case CEC::CEC_USER_CONTROL_CODE_BACKWARD:
				--next; break;
			    case CEC::CEC_USER_CONTROL_CODE_VOLUME_UP:
				++volumeAdjustment; break;
			    case CEC::CEC_USER_CONTROL_CODE_VOLUME_DOWN:
				--volumeAdjustment; break;
			    case CEC::CEC_USER_CONTROL_CODE_MUTE:
				toggleMute = !toggleMute; break;
			    default:
				break;
			}
		    }
		}
	    }
	} catch (boost::system::system_error & e) {
	    if (boost::system::errc::resource_unavailable_try_again
		    != e.code()) {
		std::cerr << e.what() << std::endl;
		std::cerr << "\tCEC pipe error, exiting" << std::endl;
		g_main_loop_quit(loop.get());
	    }
	}
	// perform batched up operations
	if (play) {
	    0 > play ? output.pause() : output.play();
	}
	if (next) {
	    0 > next ? output.previous() : output.next();
	}
	if (volumeAdjustment) {
	    output.setRelativeVolume(2 * volumeAdjustment);
	}
	if (toggleMute) {
	    output.toggleMute();
	}
	return true;
    }
    static gboolean inputThat(
	GIOChannel *	source,
	GIOCondition	condition,
	gpointer	that)
    {
	return static_cast<CecInput *>(that)->input(source, condition);
    }
    size_t				verbose;
    Adapter				adapter;
    Pipe				pipe;
    Channel				channel;
    boost::shared_ptr<GMainLoop>	loop;
    Output &				output;
public:
    CecInput(
	size_t				verbose_,
	char const *			name,
	char const *			port,
	uint32_t			timeout,
	boost::shared_ptr<GMainLoop>	loop_,
	Output &			output_)
    :
	verbose(verbose_),
	adapter(this, name, port, timeout),
	pipe(),
	channel(pipe.fds[0]),
	loop(loop_),
	output(output_)
    {
	g_io_add_watch(channel, G_IO_IN, inputThat, this);
    }
};

int main(int argc, char ** argv) {
    static std::string const helpOption		("help");
    static std::string const helpOptions	( helpOption		+ ",h");
    static std::string const manOption		("man");
    static std::string const manOptions		( manOption		+ ",m");
    static std::string const cecOption		("cec");
    static std::string const cecOptions		( cecOption		+ ",c");
    static std::string const interfaceOption	("interface");
    static std::string const interfaceOptions	( interfaceOption	+ ",i");
    static std::string const lircrcOption	("lircrc");
    static std::string const lircrcOptions	( lircrcOption		+ ",l");
    static std::string const nameOption		("name");
    static std::string const nameOptions	( nameOption		+ ",n");
    static std::string const programOption	("program");
    static std::string const programOptions	( programOption		+ ",p");
    static std::string const rendererOption	("renderer");
    static std::string const rendererOptions	( rendererOption	+ ",r");
    static std::string const serverOption	("server");
    static std::string const serverOptions	( serverOption		+ ",s");
    static std::string const timeoutOption	("timeout");
    static std::string const timeoutOptions	( timeoutOption		+ ",t");
    static std::string const verboseOption	("verbose");
    static std::string const verboseOptions	( verboseOption		+ ",v");

    static std::string const cecDefault		("");
    static std::string const interfaceDefault	("");
    static std::string const lircrcDefault	("");
    static unsigned int const serverDefault	(0);
    static std::string const rendererDefault	("(?i).*\\s-\\ssonos\\s.*");
    static unsigned int const timeoutDefault	(10000);

    try {
	char const * slash = strrchr(*argv, '/');
	char const * basename = slash ? slash + 1 : *argv;
	std::string const programDefault	(basename);
	std::string const nameDefault		(basename);

	std::ostringstream cecUsage; cecUsage
	    << "CEC adapter com port (see cec-client -l output) (default: "
	    << cecDefault << "); \"\" => default, \"-\" => no CEC input.";
	std::ostringstream interfaceUsage; interfaceUsage
	    << "UPnP network (default: "
	    << (interfaceDefault.empty()
		? "first non LOOPBACK interface"
		: interfaceDefault)
	    << ").";
	std::ostringstream lircrcUsage; lircrcUsage
	    << "lircrc file (default: "
	    << lircrcDefault << "); \"\" => default, \"-\" => no lirc input.";
	std::ostringstream nameUsage; nameUsage
	    << "CEC OSD name (default: "
	    << nameDefault << ").";
	std::ostringstream serverUsage; serverUsage
	    << "UPnP TCP SOAP server port (default: "
	    << serverDefault << "); 0 => any port).";
	std::ostringstream programUsage; programUsage
	    << "lircrc program tag (default: "
	    << programDefault << ").";
	std::ostringstream rendererUsage; rendererUsage
	    << "renderer pattern (default: "
	    << rendererDefault << ").";
	std::ostringstream timeoutUsage; timeoutUsage
	    << "CEC connection timeout in milliseconds (default: "
	    << timeoutDefault << ").";

	boost::program_options::options_description options("Options");
	    options.add_options()
		(helpOptions.c_str(),
		    "Print options usage.")
		(manOptions.c_str(),
		    "Print man(ual) page.")
		(cecOptions.c_str(),
		    boost::program_options::value<std::string>(),
		    cecUsage.str().c_str())
		(interfaceOptions.c_str(),
		    boost::program_options::value<std::string>(),
		    interfaceUsage.str().c_str())
		(lircrcOptions.c_str(),
		    boost::program_options::value<std::string>(),
		    lircrcUsage.str().c_str())
		(nameOptions.c_str(),
		    boost::program_options::value<std::string>(),
		    nameUsage.str().c_str())
		(programOptions.c_str(),
		    boost::program_options::value<std::string>(),
		    programUsage.str().c_str())
		(rendererOptions.c_str(),
		    boost::program_options::value<std::string>(),
		    rendererUsage.str().c_str())
		(serverOptions.c_str(),
		    boost::program_options::value<unsigned int>(),
		    serverUsage.str().c_str())
		(timeoutOptions.c_str(),
		    boost::program_options::value<unsigned int>(),
		    timeoutUsage.str().c_str())
		(verboseOptions.c_str(),
		    "Print trace messages.")
	;
	boost::program_options::variables_map variablesMap;
	boost::program_options::store(
		boost::program_options::parse_command_line(argc, argv, options),
	    variablesMap);
	boost::program_options::notify(variablesMap);

	if (variablesMap.count(helpOption)) {
	    std::cout << options;
	    return 0;
	}

	std::string program(variablesMap.count(programOption)
	    ? variablesMap[programOption].as<std::string>()
	    : programDefault);

	if (variablesMap.count(manOption)) {
	    std::cout <<
"NAME\n"
"       " << basename << " - Remote to UPnP AV protocol adapter\n"
"\n"
"SYNOPSIS\n"
"       " << basename << " [Option]...\n"
"\n"
"DESCRIPTION\n"
"	Convert remote control codes\n"
"	relayed from a HDMI Consumer Electronics Control (CEC) equipment or\n"
"	received from the Linux Infrared Remote Control (LIRC) daemon (lircd)\n"
"	(and adapted by program specific LIRC Run Commands (lircrc))\n"
"	to Universal Plug and Play (UPnP) Audio/Video (AV) device operations.\n"
"\n"
"	A libcec compatible HDMI CEC protocol adapter must be installed\n"
"	to relay remote/button codes from your remote.\n"
"	Specify --cec=- if there is no such input.\n"
"\n"
"	The LIRC daemon (lircd) must be configured (and running)\n"
"	to receive remote/button codes for your remote.\n"
"	These remote/button codes must then be adapted/configured\n"
"	(by way of a lircrc file specified on the command line\n"
"	or found in one of the default locations)\n"
"	to operations supported by this program (" << program << ").\n"
"	Specify --lircrc=- if there is no such input.\n"
"\n"
"	The UPnP media renderer device(s) to be targeted by each operation\n"
"	are specified as those whose friendly names\n"
"	match a renderer regular expression pattern.\n"
"\n"
"	Use the verbose program option to trace program operations.\n"
"	This includes UPnP device discovery with device friendly names\n"
"	reported for all available devices.\n"
"	Once these names are known, one should be able to specify a pattern\n"
"	to match only the device(s) of interest.\n"
"	If nothing is discovered, make sure the correct network interface\n"
"	is chosen and that UPnP network protocols are not firewalled.\n"
"	With use with a firewall, a rule to allow traffic from the local\n"
"	network to the specified UPnP TCP SOAP server port should be added.\n"
"	Unfortunately, for UPnP discovery, an ephemeral UDP port is used.\n"
"	Since this port cannot be known/specified in advance, the firewall\n"
"	must allow all UDP incoming traffic to the ephemeral port range.\n"
"\n"
	    << options
	    <<
"\n"
"LIRCRC EXAMPLES\n"
"	The following lircrc file maps typical (standard, irrecord named)\n"
"	lircd (remote/button) codes to supported (config'ed)\n"
"	program (" << program << ") operations:\n"
"		begin\n"
"			prog	= " << program << "\n"
"			button	= KEY_PAUSE\n"
"			config	= Pause\n"
"			repeat	= 0\n"
"		end\n"
"		begin\n"
"			prog	= " << program << "\n"
"			button	= KEY_PLAY\n"
"			config	= Play\n"
"			repeat	= 0\n"
"		end\n"
"		begin\n"
"			prog	= " << program << "\n"
"			button	= KEY_REWIND\n"
"			config	= Previous\n"
"			repeat	= 0\n"
"		end\n"
"		begin\n"
"			prog	= " << program << "\n"
"			button	= KEY_FASTFORWARD\n"
"			config	= Next\n"
"			repeat	= 0\n"
"		end\n"
"		begin\n"
"			prog	= " << program << "\n"
"			button	= KEY_MUTE\n"
"			config	= Mute\n"
"			repeat	= 0\n"
"		end\n"
"		begin\n"
"			prog	= " << program << "\n"
"			button	= KEY_VOLUMEUP\n"
"			config	= VolumeUp\n"
"			repeat	= 1\n"
"		end\n"
"		begin\n"
"			prog	= " << program << "\n"
"			button	= KEY_VOLUMEDOWN\n"
"			config	= VolumeDown\n"
"		 	repeat	= 1\n"
"		end\n"
"\n"
"SEE ALSO\n"
"	cec-client\n"
"	irrecord(1)\n"
"	irw(1)\n"
"	lircd(8)\n"
"	gupnp-av-cp\n"
"	gupnp-universal-cp\n"
;
	    return 0;
	}

	std::string cec(variablesMap.count(cecOption)
	    ? variablesMap[cecOption].as<std::string>()
	    : cecDefault);
	std::string interface(variablesMap.count(interfaceOption)
	    ? variablesMap[interfaceOption].as<std::string>()
	    : interfaceDefault);
	std::string lircrc(variablesMap.count(lircrcOption)
	    ? variablesMap[lircrcOption].as<std::string>()
	    : lircrcDefault);
	std::string name(variablesMap.count(nameOption)
	    ? variablesMap[nameOption].as<std::string>()
	    : nameDefault);
	std::string renderer(variablesMap.count(rendererOption)
	    ? variablesMap[rendererOption].as<std::string>()
	    : rendererDefault);
	unsigned int server(variablesMap.count(serverOption)
	    ? variablesMap[serverOption].as<unsigned int>()
	    : serverDefault);
	unsigned int timeout(variablesMap.count(timeoutOption)
	    ? variablesMap[timeoutOption].as<unsigned int>()
	    : timeoutDefault);
	size_t verbose = variablesMap.count(verboseOption);

	boost::shared_ptr<GMainLoop> loop(
	    g_main_loop_new(0, true),
	    g_main_loop_unref);

	// glue inputs and output together
	Output output(
	    verbose,
	    interface.empty() ? 0 : interface.c_str(),
	    server,
	    renderer);
	boost::shared_ptr<CecInput> cecInput;
	if ("-" != cec) {
	    cecInput.reset(new CecInput(
		verbose,
		name.c_str(),
		cec.empty() ? 0 : cec.c_str(),
		timeout,
		loop,
		output));
	}
	boost::shared_ptr<LircInput> lircInput;
	if ("-" != lircrc) {
	    lircInput.reset(new LircInput(
		verbose,
		program.c_str(),
		lircrc.empty() ? 0 : lircrc.c_str(),
		loop,
		output));
	}

	g_main_loop_run(loop.get());

    } catch (std::exception & e) {
	std::cerr << e.what() << std::endl;
	return -1;
    }
    return 0;
}
