#include <windows.h>
#include <iostream>
#include <boost/timer.hpp>
#include <boost/program_options.hpp>
#include <boost/iostreams/device/null.hpp>
#include <boost/iostreams/stream.hpp>

// null ostream
boost::iostreams::stream_buffer<boost::iostreams::null_sink>	nsink;
std::ostream nstrm( &nsink );
// error ostream
std::ostream* estrm;

const char Desc[] = {
	"Windows Service Control Program ver 0.1\n" \
	"---------------------------------------\n" \
	" net_service [ options ... ]\n" \
	"sample: \n" \
	"   > net_service -n \"W3SVC\" --start --noerror --silent\n" \
	"   > net_service --name \"W3SVC\" --stop\n" \
	"result: \n" \
	"     0: success\n" \
	"     1: invalid option\n" \
	"     2: Can't open Service Control Manager\n" \
	"     3: Can't open Service\n" \
	"     4: Can't get Service Status\n" \
	"     5: Can't start Service\n" \
	"     6: Can't stop Service\n" \
	"     7: Timeout error\n" \
	"allowed options"
};

bool query_status( SC_HANDLE hSvc, DWORD& dwStatus ) {
	SERVICE_STATUS status;
	if( QueryServiceStatus( hSvc, &status) == FALSE) {
		*estrm << "Can't get Service Status" << std::endl;
		return false;
	}
	dwStatus	=	status.dwCurrentState;
	return true;
}

void CloseHandles( SC_HANDLE& hSC, SC_HANDLE& hSvc ) {
	if( hSvc )	CloseServiceHandle(hSvc);
	if( hSC )		CloseServiceHandle(hSC);
	hSvc	=	NULL;
	hSC		=	NULL;
}

enum start_ctrl {
	invalid_control	=	0,
	start_service,
	stop_service
};

int main( int argc, char* argv[] ) {
	bool noerror	=	false;
	estrm	=	&std::cerr;
	start_ctrl ctrl	=	invalid_control;
	std::string	service_name;
	unsigned limits = 0;

	try {
		boost::program_options::options_description desc( Desc );
		desc.add_options()
			("help,h", "Procedure help message")
			("name,n", boost::program_options::value<std::string>(), "Service Name. (required)")
			("stop", "stop service")
			("start", "start service")
			("noerror", "always return 0. (=success)")
			("silent,s", "silent mode: no report of errors and warnings")
			("limits,t", boost::program_options::value<unsigned>(), "wait timeout sec. (default 0: forever)")
		;

		boost::program_options::variables_map	vm;
		boost::program_options::store( 
			boost::program_options::parse_command_line( argc, argv, desc ),
			vm
		);
		boost::program_options::notify( vm );

		if( vm.count("noerror") )	noerror	=	true;

		if( vm.count("help") || argc <= 1 ) {
			std::cout << desc << std::endl;
			return (noerror) ? 0 : 1;
		}

		// invoke log option
		if( vm.count("silent") ) {
			nsink.open( boost::iostreams::null_sink() );
			estrm	=	&nstrm;
		}

		if( vm.count("start") ) {
			ctrl	=	start_service;
		}
		if( vm.count("stop") ) {
			if( ctrl ) {
				*estrm << "duplicate control options" << std::endl;
				return (noerror) ? 0 : 1;
			}
			ctrl	=	stop_service;
		}

		if( vm.count("name") ) {
			service_name	=	vm["name"].as<std::string>();
		}

		if( vm.count("limits") ) {
			limits = vm["limits"].as<unsigned>();
		}

		SC_HANDLE hSC = OpenSCManager( NULL, NULL, GENERIC_EXECUTE);
		if( hSC == NULL) {
			*estrm << "Can't open Service Control Manager" << std::endl;
			return (noerror) ? 0 : 2;
		}

		SC_HANDLE hSvc = OpenService( 
			hSC, service_name.c_str(), SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_STOP 
		);
		if( hSvc == NULL) {
			*estrm << "Can't open Service: \"" << service_name << "\"" << std::endl;
			CloseHandles( hSC, hSvc );
			return (noerror) ? 0 : 3;
		}

		DWORD dwStatus;
		if( !query_status( hSvc, dwStatus ) ) {
			CloseHandles( hSC, hSvc );
			return (noerror) ? 0 : 4;
		}

		boost::timer t;

		switch( ctrl ) {
		case start_service: {
			if( dwStatus != SERVICE_RUNNING ) {
				if( StartService( hSvc, NULL, NULL) == FALSE) {
					*estrm << "Can't start Service: \"" << service_name << "\"" << std::endl;
					CloseHandles( hSC, hSvc );
					return (noerror) ? 0 : 5;
				}
				while( query_status( hSvc, dwStatus ) ) {
					Sleep( 250 );
					std::cout << ".";
					if( dwStatus == SERVICE_RUNNING) {
						std::cout << std::endl << "\"" << service_name << "\" was started" << std::endl;
						break;
					}
					if( limits && t.elapsed() >= limits ) {
						*estrm << "Timeouts occured: " << t.elapsed() << " sec." << std::endl;
						return (noerror) ? 0 : 7;
					}
				}
			} else {
				std::cout << "\"" << service_name << "\" is already started." << std::endl;
			}
			break;
											}
		case stop_service: {
			if( dwStatus != SERVICE_STOPPED ) {
				SERVICE_STATUS	stat;
				if( ControlService( hSvc, SERVICE_CONTROL_STOP, &stat) == FALSE) {
					*estrm << "Can't stop service: \"" << service_name << "\"" << std::endl;
					CloseHandles( hSC, hSvc );
					return (noerror) ? 0 : 6;
				}
				while( query_status( hSvc, dwStatus ) ) {
					Sleep( 250 );
					std::cout << ".";
					if( dwStatus == SERVICE_STOPPED) {
						std::cout << std::endl << "\"" << service_name << "\" was stopped." << std::endl;
						break;
					}
					if( limits && t.elapsed() >= limits ) {
						*estrm << "Timeouts occured: " << t.elapsed() << " sec." << std::endl;
						return (noerror) ? 0 : 7;
					}
				}
			} else {
				std::cout << "\"" << service_name << "\" is already stopped."  << std::endl;
			}
			break;
											}
		default: {
			*estrm << "no control options" << std::endl;
			CloseHandles( hSC, hSvc );
			return (noerror) ? 0 : 1;
						}
		}
		CloseHandles( hSC, hSvc );
		return 0;

	} catch( std::exception& e ) {
		*estrm << "error: " << e.what() << std::endl;
	} catch( ... ) {
		*estrm << "unknown error" << std::endl;
	}

	return (noerror) ? 0 : 1;
}

