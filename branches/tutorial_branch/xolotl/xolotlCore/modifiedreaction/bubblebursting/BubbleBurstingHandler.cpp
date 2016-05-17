// Includes
#include "BubbleBurstingHandler.h"

namespace xolotlCore {

void BubbleBurstingHandler::initialize(int surfacePos, PSIClusterReactionNetwork *network,
		std::vector<double> grid) {
	// Add the needed reaction connectivity
	// Each V cluster connects to every HeV clusters with the same number of V

	// Get all the V clusters from the network
	auto vClusters = network->getAll(vType);
	// Get all the HeV bubbles from the network
	auto bubbles = network->getAll(heVType);
	// Loop on the V clusters
	for (int i = 0; i < vClusters.size(); i++) {
		// Get the cluster and its size
		auto cluster = (PSICluster *) vClusters[i];
		int vSize = cluster->getSize();

		// Loop on the bubbles
		for (int j = 0; j < bubbles.size(); j++) {
			// Get the bubble and its composition
			auto bubble =  (PSICluster *) bubbles[j];
			auto comp = bubble->getComposition();

			// Connect if their vSize is the same
			if (comp[vType] == vSize) {
				cluster->setReactionConnectivity(bubble->getId());
			}
		}
	}

	// Method that will fill the index vector that is actually used during the solving steps
	initializeIndex(surfacePos, network, grid);

	// Update the bubble bursting rate
	updateBurstingRate(network);

	return;
}

void BubbleBurstingHandler::initializeIndex(int surfacePos, PSIClusterReactionNetwork *network,
		std::vector<double> grid) {
	// Clear the vector of HeV bubble bursting at each grid point
	indexVector.clear();

	// Get all the HeV bubbles from the network
	auto bubbles = network->getAll(heVType);

	// Loop on the grid points located to the right of the surface
	for (int i = 0; i < grid.size(); i++) {
		// Create the list (vector) of indices at this grid point
		std::vector<int> indices;

		// Nothing happens on the left side of the surface
		if (i <= surfacePos) {
			indexVector.push_back(indices);
			continue;
		}

		// Get the depth
		double depth = grid[i] - grid[surfacePos];

		// Loop on all the bubbles
		for (int j = 0; j < bubbles.size(); j++) {
			// Get the bubble and its radius
			auto bubble = (PSICluster *) bubbles[j];
			double radius = bubble->getReactionRadius();
			auto comp = bubble->getComposition();

			if (comp[heType] < comp[vType] * 3) continue;

			// If the radius is bigger than the distance to the surface there is bursting
			if (radius > depth) {
				// Add the bubble index to the list of indices
				indices.push_back(j);
			}
		}

		// Add indices to the index vector
		indexVector.push_back(indices);
	}

	return;
}

void BubbleBurstingHandler::updateBurstingRate(PSIClusterReactionNetwork *network) {
	// Get all the HeV bubbles from the network
	auto bubbles = network->getAll(heVType);

	// Update the rate by finding the biggest rate in all the HeV bubbles
	kBursting = 0.0;
	// Loop on the bubbles
	for (int j = 0; j < bubbles.size(); j++) {
		// Get the bubble and its rate
		auto bubble =  (PSICluster *) bubbles[j];
		double rate = bubble->getBiggestRate();

		// Compare it to the bursting rate
		if (kBursting < rate) kBursting = rate;
	}
	// Multiply it by 1000.0
	kBursting = 1000.0 * kBursting;

	return;
}

void BubbleBurstingHandler::computeBursting(PSIClusterReactionNetwork *network,
		int xi, double *concOffset, double *updatedConcOffset) {
	// Get all the HeV bubbles
	auto bubbles = network->getAll(heVType);

	// Get the pointer to list of indices at this grid point
	std::vector<int> * indices = &indexVector[xi];
	// Loop on the list
	for (int i = 0; i < indices->size(); i++) {
		// Get the stored bubble and its ID
		auto bubble = (PSICluster *) bubbles[indices->at(i)];
		int bubbleIndex = bubble->getId() - 1;

		// Get the initial concentration
		double oldConc = concOffset[bubbleIndex];

		// Go to the next bubble if this concentration is 0.0
		if (oldConc == 0.0) continue;

		// Get the V cluster with the same number of V
		auto comp = bubble->getComposition();
		auto vCluster = (PSICluster *) network->get(vType, comp[vType]);
		// And its ID
		int vIndex = vCluster->getId() - 1;

		// Update the concentrations (the bubble loses its concentration)
		updatedConcOffset[bubbleIndex] -= kBursting * oldConc;
		updatedConcOffset[vIndex] += kBursting * oldConc;
	}

	return;
}

int BubbleBurstingHandler::computePartialsForBursting(PSIClusterReactionNetwork *network,
		double *val, int *indices, int xi) {
	// Get all the HeV bubbles
	auto bubbles = network->getAll(heVType);
	// Get the size of the network
	int size = network->getAll()->size();

	// Get the pointer to list of indices at this grid point
	std::vector<int> * clusterIndices = &indexVector[xi];
	// Loop on the list
	for (int i = 0; i < clusterIndices->size(); i++) {
		// Get the stored bubble and its ID
		auto bubble = (PSICluster *) bubbles[clusterIndices->at(i)];
		int bubbleIndex = bubble->getId() - 1;

		// Keep the bubble index
		indices[i * 2] = bubbleIndex;
		// Set its partial derivative
		val[i * 2] = -kBursting;

		// Get the V cluster with the same number of V
		auto comp = bubble->getComposition();
		auto vCluster = (PSICluster *) network->get(vType, comp[vType]);
		// And its ID
		int vIndex = vCluster->getId() - 1;
		// Set its partial derivative
		indices[(i * 2) + 1] = vIndex;
		val[(i * 2) + 1] = kBursting;
	}

	return clusterIndices->size();
}

}/* end namespace xolotlCore */

