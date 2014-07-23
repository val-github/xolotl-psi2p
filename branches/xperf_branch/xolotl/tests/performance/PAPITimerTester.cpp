#define BOOST_TEST_MODULE Regression

#include <string>
#include <boost/test/included/unit_test.hpp>
#include "papi.h"
#include "xolotlPerf/papi/PAPITimer.h"


using namespace std;
using namespace xolotlPerf;




/**
 * This suite is responsible for testing the PAPITimer.
 */


// Normally, PAPI would be initialized by the HandlerRegistry.
// Since our purpose is to test the Timer class and not the registry,
// we recreate the initialization explicitly.
bool
InitializePAPI( void )
{
    bool ret = true;

    if( !PAPI_is_initialized() )
    {
        int papiVersion = PAPI_library_init(PAPI_VER_CURRENT);
        if( papiVersion != PAPI_VER_CURRENT )
        {
            BOOST_TEST_MESSAGE("PAPI library version mismatch: asked for" << PAPI_VER_CURRENT << ", got " << papiVersion);
            ret = false;
        }
    }
    return ret;
}




BOOST_AUTO_TEST_SUITE (PAPITimer_testSuite)

BOOST_AUTO_TEST_CASE(checkName) {

    bool papiInitialized = InitializePAPI();
    BOOST_REQUIRE_EQUAL(papiInitialized, true);

    PAPITimer tester("test");

    BOOST_TEST_MESSAGE( "\n" << "PAPITimer Message: \n" << "tester.getName() = " << tester.getName() << "\n"
                  );

    //Require that the name of this Timer is "test"
    BOOST_REQUIRE_EQUAL("test", tester.getName());
}


BOOST_AUTO_TEST_CASE(checkTiming) {

    bool papiInitialized = InitializePAPI();
    BOOST_REQUIRE_EQUAL(papiInitialized, true);

	PAPITimer tester("test");
	double sleepSeconds = 2.0;

	//Output the version of PAPI that is being used
	BOOST_TEST_MESSAGE("\n" << "PAPI_VERSION = " << PAPI_VERSION_MAJOR(PAPI_VERSION) << "."
			  << PAPI_VERSION_MINOR(PAPI_VERSION) << "." << PAPI_VERSION_REVISION(PAPI_VERSION) << "\n");


	double wall, usr, sys;
	double wallStart, wallStop;

    // Simulate some computation/communication with a sleep of known duration.
    // Time the duration of the operation.
	tester.start();
	sleep(sleepSeconds);
	tester.stop();

	//Output the difference between the wallclock timestamps when the timer was started and stopped
	BOOST_TEST_MESSAGE( "\n" << "PAPITimer Message: \n" << "tester.getName() = " << tester.getName() << "\n"
			  << "tester.getValue() = " << tester.getValue() << "s" << "\n"
			  << "tester.getValue() - " << sleepSeconds << "s = " << tester.getValue()-sleepSeconds << "s");

	// Require that the value of this Timer is within 3% of the 
    // duration of the sleep.
	BOOST_REQUIRE_CLOSE(sleepSeconds, tester.getValue(),0.03);
}

BOOST_AUTO_TEST_CASE(checkUnits)
{
    bool papiInitialized = InitializePAPI();
    BOOST_REQUIRE_EQUAL(papiInitialized, true);

	PAPITimer tester("test");
	BOOST_REQUIRE_EQUAL("s", tester.getUnits());
}

BOOST_AUTO_TEST_SUITE_END()
