#ifndef IBUBBLEBURSTINGHANDLER_H
#define IBUBBLEBURSTINGHANDLER_H

// Includes
#include <PSICluster.h>
#include <PSIClusterReactionNetwork.h>
#include <memory>

namespace xolotlCore {

/**
 * Realizations of this interface are responsible for all the physical parts
 * for the bursting of HeV bubbles. A HeV bubble bursts when it is close to the surface,
 * and looses all its helium atoms. The solver call these methods to handle
 * the bubble bursting.
 */
class IBubbleBurstingHandler {

public:

	/**
	 * The destructor
	 */
	virtual ~IBubbleBurstingHandler() {}

	/**
	 * The initialize method has to add connectivity between the V clusters and HeV clusters
	 * of same number of V. It must also initialize the rates of the reactions and define
	 * which bubbles can burst at each grid point.
	 *
	 * @param network The network
	 * @param hx The grid step size
	 * @param nGrid The number of points on the grid
	 * @param surfacePos The index of the position on the surface
	 */
	virtual void initialize(std::shared_ptr<PSIClusterReactionNetwork> network, double hx,
			int nGrid, int surfacePos) = 0;

	/**
	 * Compute the flux due to the bubble bursting for all the cluster,
	 * given the position index xi.
	 * This method is called by the RHSFunction from the PetscSolver.
	 *
	 * @param network The network
	 * @param xi The index of the position on the grid
	 * @param surfacePos The index of the position on the surface
	 * @param concOffset The pointer to the array of concentration at the grid
	 * point where the advection is computed
	 * @param updatedConcOffset The pointer to the array of the concentration at the grid
	 * point where the advection is computed used to find the next solution
	 */
	virtual void computeBursting(std::shared_ptr<PSIClusterReactionNetwork> network,
			int xi, int surfacePos, double *concOffset, double *updatedConcOffset) = 0;

	/**
	 * Compute the partials due to the bubble bursting for all the clusters given
	 * the position index xi. Returns the number of bubbles that can possibly burst.
	 * This method is called by the RHSJacobian from the PetscSolver.
	 *
	 * @param network The network
	 * @param val The pointer to the array that will contain the values of partials
	 * for the advection
	 * @param row The pointer to the array that will contain the indices of the row
	 * for the Jacobian
	 * @param col The pointer to the array that will contain the indices of the columns
	 * for the Jacobian
	 * @param xi The index of the grip point
	 * @param xs The index of the first grid point on the locally owned grid
	 * @param surfacePos The index of the position on the surface
	 *
	 * @return The number of bubbles that can burst at this grid point
	 */
	virtual int computePartialsForBursting(std::shared_ptr<PSIClusterReactionNetwork> network,
			double *val, int *row, int *col, int xi, int xs, int surfacePos) = 0;

};
//end class IBubbleBurstingHandler

} /* namespace xolotlCore */
#endif
