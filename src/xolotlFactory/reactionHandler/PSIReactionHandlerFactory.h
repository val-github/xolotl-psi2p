#ifndef PSIREACTIONHANDLERFACTORY_H
#define PSIREACTIONHANDLERFACTORY_H

#include <memory>
#include "IReactionHandlerFactory.h"
#include <HDF5NetworkLoader.h>
#include <PSIClusterReactionNetwork.h>

namespace xolotlFactory {

/**
 * Realizes the IReactionHandlerFactory interface. Handles the network for a PSI problem.
 */
class PSIReactionHandlerFactory: public IReactionHandlerFactory {
protected:

	//! The network loader handler
	std::shared_ptr<xolotlCore::INetworkLoader> theNetworkLoaderHandler;

	//! The network handler
	std::shared_ptr<xolotlCore::IReactionNetwork> theNetworkHandler;

public:

	/**
	 * The constructor creates the handlers.
	 */
	PSIReactionHandlerFactory() {
	}

	/**
	 * The destructor
	 */
	~PSIReactionHandlerFactory() {
	}

	/**
	 * Initialize the reaction network.
	 *
	 * @param options The options.
	 * @param registry The performance registry.
	 */
	void initializeReactionNetwork(xolotlCore::Options &options,
			std::shared_ptr<xolotlPerf::IHandlerRegistry> registry) {
		// Get the current process ID
		int procId;
		MPI_Comm_rank(MPI_COMM_WORLD, &procId);

		// Create a HDF5NetworkLoader
		auto tempNetworkLoader = std::make_shared<xolotlCore::HDF5NetworkLoader>(registry);
		// Give the networkFilename to the network loader
		tempNetworkLoader->setFilename(options.getNetworkFilename());
		// Set the options for the grouping scheme
		tempNetworkLoader->setVMin(options.getGroupingMin());
		tempNetworkLoader->setHeWidth(options.getGroupingWidthA());
		tempNetworkLoader->setVWidth(options.getGroupingWidthB());
		theNetworkLoaderHandler = tempNetworkLoader;

		// Check if we want dummy reactions
		auto map = options.getProcesses();
		if (!map["reaction"]) theNetworkLoaderHandler->setDummyReactions();
		// Load the network
		theNetworkHandler = theNetworkLoaderHandler->load();

		if (procId == 0) {
			std::cout << "\nFactory Message: " << "Master loaded network of size "
					<< theNetworkHandler->size() << "." << std::endl;
		}
	}

	/**
	 * Return the network loader.
	 *
	 * @return The network loader.
	 */
	std::shared_ptr<xolotlCore::INetworkLoader> getNetworkLoaderHandler() const {
		return theNetworkLoaderHandler;
	}

	/**
	 * Return the network.
	 *
	 * @return The network.
	 */
	std::shared_ptr<xolotlCore::IReactionNetwork> getNetworkHandler() const {
		return theNetworkHandler;
	}

};

} // end namespace xolotlFactory

#endif // PSIREACTIONHANDLERFACTORY_H
