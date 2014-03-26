#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Regression

#include <boost/test/included/unit_test.hpp>
#include <DataProvider.h>

using namespace std;
using namespace xolotlViz;

/**
 * This suite is responsible for testing the DataProvider.
 */
BOOST_AUTO_TEST_SUITE(DataProvider_testSuite)

/**
 * Method checking you can add points to the data, get the data, and getDataMean().
 */
BOOST_AUTO_TEST_CASE(checkData) {

	// Create myDataProvider
	shared_ptr<DataProvider> myDataProvider(
			new DataProvider());

	// Create a Point vector
	shared_ptr< vector<xolotlViz::Point> > myPoints(
			new (vector<xolotlViz::Point>));

	// And fill it with some Point
	Point aPoint;
	aPoint.value = 3.; aPoint.t = 1.; aPoint.x = 2.;
	myPoints->push_back(aPoint);
	aPoint.value = 2.; aPoint.t = 3.; aPoint.x = 2.;
	myPoints->push_back(aPoint);
	aPoint.value = 5.; aPoint.t = 6.; aPoint.x = -2.;
	myPoints->push_back(aPoint);
	aPoint.value = -8.; aPoint.t = 8.; aPoint.x = 5.;
	myPoints->push_back(aPoint);
	aPoint.value = 7.; aPoint.t = 7.; aPoint.x = 7.;
	myPoints->push_back(aPoint);

	// Set these points in the myDataProvider
	myDataProvider->setPoints(myPoints);

	// Get them back
	auto dataPoints = myDataProvider->getDataPoints();

	// First check the size of the vector
	BOOST_REQUIRE_EQUAL(dataPoints->size(), myPoints->size());

	// Loop on all the points in dataPoints
	for (int i = 0; i < dataPoints->size(); i++) {

		// Check that all the fields are the same
		BOOST_REQUIRE_EQUAL(dataPoints->at(i).value, myPoints->at(i).value);
		BOOST_REQUIRE_EQUAL(dataPoints->at(i).t, myPoints->at(i).t);
		BOOST_REQUIRE_EQUAL(dataPoints->at(i).x, myPoints->at(i).x);
		BOOST_REQUIRE_EQUAL(dataPoints->at(i).y, myPoints->at(i).y);
		BOOST_REQUIRE_EQUAL(dataPoints->at(i).z, myPoints->at(i).z);
	}

	// Get the mean value of the data
	auto mean = myDataProvider->getDataMean();

	// Check it is the right mean value:
	// (3 + 2 + 5 - 8 + 7) / 5
	BOOST_REQUIRE_EQUAL(mean, 1.8);
}

BOOST_AUTO_TEST_SUITE_END()
