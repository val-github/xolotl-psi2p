#ifndef SURFACEADVECTIONHANDLER_H
#define SURFACEADVECTIONHANDLER_H

// Includes
#include "AdvectionHandler.h"
#include <MathUtils.h>

namespace xolotlCore {

/**
 * This class realizes the IAdvectionHandler interface responsible for all
 * the physical parts for the advection of mobile helium cluster, in the case
 * where cluster are drifting toward the surface.
 */
class SurfaceAdvectionHandler: public AdvectionHandler {
public:

	//! The Constructor
	SurfaceAdvectionHandler() {}

	//! The Destructor
	~SurfaceAdvectionHandler() {}

	/**
	 * Set the position of the surface.
	 *
	 * @param pos The position of the surface
	 */
	void setPosition(double pos);

	/**
	 * Compute the flux due to the advection for all the helium clusters,
	 * given the space parameter hx and the position.
	 * This method is called by the RHSFunction from the PetscSolver.
	 *
	 * If D is the diffusion coefficient, and C_r, C_m the right and middle concentration
	 * of this cluster, A the sink strength, K the Boltzmann constant, T the temperature,
	 * the value to add to the updated concentration is:
	 *
	 * [(3 * A * D) / (K * T * hx)] * [(C_r / [pos_x + hx]^4) - (C_m / (pos_x)^4)]
	 *
	 * @param network The network
	 * @param pos The position on the grid
	 * @param concVector The pointer to the pointer of arrays of concentration at middle,
	 * left, and right grid points
	 * @param updatedConcOffset The pointer to the array of the concentration at the grid
	 * point where the advection is computed used to find the next solution
	 * @param hxLeft The step size on the left side of the point in the x direction
	 * @param hxRight The step size on the right side of the point in the x direction
	 * @param hy The step size in the y direction
	 * @param hz The step size in the z direction
	 */
	void computeAdvection(PSIClusterReactionNetwork *network,
			std::vector<double> &pos, double **concVector, double *updatedConcOffset,
			double hxLeft, double hxRight, double hy = 0.0, double hz = 0.0);

	/**
	 * Compute the partials due to the advection of all the helium clusters given
	 * the space parameter hx and the position.
	 * This method is called by the RHSJacobian from the PetscSolver.
	 *
	 * The partial derivative on the right grid point is given by (same notation as for
	 * the computeAdvection method)
	 *
	 * (3 * A * D) / [K * T * hx * (pos_x + hx)^4]
	 *
	 * and on this grid point we have
	 *
	 * - (3 * A * D) / [K * T * hx * (pos_x)^4]
	 *
	 * @param network The network
	 * @param val The pointer to the array that will contain the values of partials
	 * for the advection
	 * @param indices The pointer to the array that will contain the indices of the
	 * advecting cluster in the network
	 * @param pos The position on the grid
	 * @param hxLeft The step size on the left side of the point in the x direction
	 * @param hxRight The step size on the right side of the point in the x direction
	 * @param hy The step size in the y direction
	 * @param hz The step size in the z direction
	 */
	void computePartialsForAdvection(PSIClusterReactionNetwork *network,
			double *val, int *indices, std::vector<double> &pos,
			double hxLeft, double hxRight, double hy = 0.0, double hz = 0.0);

	/**
	 * Compute the indices that will determine where the partial derivatives will
	 * be put in the Jacobian.
	 * This method is called by the RHSJacobian from the PetscSolver.
	 *
	 * For the surface advection the stencil is always the same:
	 *
	 * stencil[0] = 1 //x
	 * stencil[1] = 0 //y
	 * stencil[2] = 0 //z
	 *
	 * @param pos The position on the grid
	 * @return The indices for the position in the Jacobian
	 */
	std::vector<int> getStencilForAdvection(std::vector<double> &pos);

	/**
	 * Check whether the grid point is located on the sink surface or not.
	 *
	 * @param pos The position on the grid
	 * @return True if the point is on the sink
	 */
	bool isPointOnSink(std::vector<double> &pos);

};
//end class SurfaceAdvectionHandler

} /* end namespace xolotlCore */
#endif