/*
 * PSIClusterTester.cpp
 *
 *  Created on: May 6, 2013
 *      Author: Jay Jay Billings
 */
#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Regression

#include <boost/test/included/unit_test.hpp>
#include <PSICluster.h>
#include "SimpleReactionNetwork.h"
#include <HeInterstitialCluster.h>
#include <HeCluster.h>
#include <memory>
#include <typeinfo>
#include <limits>
#include <algorithm>

using namespace std;
using namespace xolotlCore;
using namespace testUtils;


/**
 * This suite is responsible for testing the HeInterstitialCluster.
 */
BOOST_AUTO_TEST_SUITE(HeInterstitialCluster_testSuite)


BOOST_AUTO_TEST_CASE(getSpeciesSize) {
	HeInterstitialCluster cluster(4, 2);

	// Get the composition back
	auto composition = cluster.getComposition();

	// Check the composition is the created one
	BOOST_REQUIRE_EQUAL(composition["He"], 4);
	BOOST_REQUIRE_EQUAL(composition["V"], 0);
	BOOST_REQUIRE_EQUAL(composition["I"], 2);
}

/**
 * This operation checks the ability of the HeInterstitialCluster to describe
 * its connectivity to other clusters.
 */
BOOST_AUTO_TEST_CASE(checkConnectivity) {

	shared_ptr<ReactionNetwork> network = testUtils::getSimpleReactionNetwork();
	auto reactants = network->getAll();
	auto props = network->getProperties();
	
	// Prevent dissociation from being added to the connectivity array
	props["dissociationsEnabled"] = "false";
	
	// Check the reaction connectivity of the HeI cluster
	// with 5He and 3I
	
	{
		// Get the connectivity array from the reactant
		std::vector<int> composition = {5, 0, 3 };
		auto reactant = std::dynamic_pointer_cast < PSICluster
				> (network->getCompound("HeI", composition));
		auto reactionConnectivity = reactant->getConnectivity();
		
		BOOST_REQUIRE_EQUAL(reactant->getComposition()["He"], 5);
		BOOST_REQUIRE_EQUAL(reactant->getComposition()["I"], 3);
		
		// Check the connectivity for He, V, and I
		
		int connectivityExpected[] = {
			// He
			1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
			
			// V
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			
			// I
			// Only single-I clusters react with HeI
			1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			
			// HeV
			0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0,
			0, 0, 0, 0,
			0, 0, 0,
			0, 0,
			0,
			
			// HeI
			0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 1, 0, 0, 0,
			0, 0, 0, 1, 1, 1, 1,
			0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0,
			0, 0, 0, 0,
			0, 0, 0,
			0, 0,
			0
		};

		for (int i = 0; i < reactionConnectivity.size(); i++) {
			BOOST_REQUIRE_EQUAL(reactionConnectivity[i], connectivityExpected[i]);
		}
	}
}

/**
 * This operation checks the ability of the HeInterstitialCluster to compute
 * the total flux.
 */
BOOST_AUTO_TEST_CASE(checkTotalFlux) {
	std::cout << "HeInterstitialClusterTester Message: \n"
			  << "BOOST_AUTO_TEST_CASE(checkTotalFlux): arbitrary values because of lack of data"
			  << "\n";

	// Local Declarations
	shared_ptr<ReactionNetwork> network = getSimpleReactionNetwork();

	// Get an HeI cluster with compostion 1,0,1.
	std::vector<int> composition = {1, 0, 1};
	auto cluster = std::dynamic_pointer_cast<PSICluster>(network->getCompound(
			"HeI",composition));
	// Get one that it combines with (V)
	auto secondCluster = std::dynamic_pointer_cast<PSICluster>(network->get("V", 1));
	// Set the diffusion factor, migration and binding energies to arbitrary
	// values because HeI does not exist in benchmarks
	cluster->setDiffusionFactor(1.5E+10);
	cluster->setMigrationEnergy(std::numeric_limits<double>::infinity());
	std::vector<double> energies = {5.09, std::numeric_limits<double>::infinity(),
			5.09, 12.6};
	cluster->setBindingEnergies(energies);
	cluster->setConcentration(0.5);

	// Set the diffusion factor, migration and binding energies based on the
	// values from the tungsten benchmark for this problem for the second cluster
	secondCluster->setDiffusionFactor(2.410E+11);
	secondCluster->setMigrationEnergy(1.66);
	energies = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
			std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
	secondCluster->setBindingEnergies(energies);
	secondCluster->setConcentration(0.5);
	// The flux can pretty much be anything except "not a number" (nan).
	double flux = cluster->getTotalFlux(1000.0);
	std::cout.precision(15);
	std::cout << "HeInterstitialClusterTester Message: \n" << "Total Flux is " << flux << "\n"
			  << "   -Production Flux: " << cluster->getProductionFlux(1000.0) << "\n"
			  << "   -Combination Flux: " << cluster->getCombinationFlux(1000.0) << "\n"
			  << "   -Dissociation Flux: " << cluster->getDissociationFlux(1000.0) << "\n";

	BOOST_FAIL( "BOOST_AUTO_TEST_CASE(checkTotalFlux): this test is not ready yet" );
}

/**
 * This operation checks the reaction radius for HeInterstitialCluster.
 */
BOOST_AUTO_TEST_CASE(checkReactionRadius) {

	std::vector<std::shared_ptr<HeInterstitialCluster>> clusters;
	std::shared_ptr<HeInterstitialCluster> cluster;
	double expectedRadii[] = { 0.1372650265, 0.1778340462, 0.2062922619,
			0.2289478080, 0.2480795532 };

	for (int i = 1; i <= 5; i++) {
		cluster = std::shared_ptr<HeInterstitialCluster>(new HeInterstitialCluster(1, i));
		BOOST_CHECK_CLOSE(expectedRadii[i - 1], cluster->getReactionRadius(),
				.000001);
	}
}


BOOST_AUTO_TEST_SUITE_END()
